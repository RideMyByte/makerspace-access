"""Web frontend API routes – read-only access + check-in/out + member creation.

Uses WEB_API_KEY which has restricted access:
- Read: first_name, nfc_ids, last_visit_at, category, safety_briefing, postal_code
- Write: check-in, check-out, create new member (no update/delete)
"""

from typing import Annotated

from app.config import get_settings
from app.db import get_db_session
from app.influx_logger import log_check_in, log_check_out
from app.models.member import Member
from app.models.nfc_id import NfcId
from app.schemas.member import (
    CheckInRequest,
    InductionItem,
    MemberCreate,
    MemberRead,
    PresenceEvent,
)
from app.security.api_key import api_key_header
from fastapi import APIRouter, Depends, HTTPException, Query, status
from pydantic import BaseModel, Field
from sqlalchemy import select
from sqlalchemy.exc import IntegrityError
from sqlalchemy.orm import Session

router = APIRouter(
    prefix="/web",
    tags=["web-frontend"],
)


# ===== Read-only schema (limited fields) =====
class WebMemberRead(BaseModel):
    id: int
    first_name: str
    nfc_ids: list[str]
    last_visit_at: str | None = None
    category: str
    safety_briefing_valid: bool
    last_safety_briefing: str | None = None
    postal_code: str | None = None
    is_present: bool

    model_config = {"from_attributes": True}


def _to_web_read(member: Member) -> WebMemberRead:
    from datetime import date, timedelta

    valid = False
    last_sb: str | None = None
    if member.last_safety_briefing:
        last_sb = member.last_safety_briefing.isoformat()
        valid = member.last_safety_briefing >= (date.today() - timedelta(days=180))

    last_visit: str | None = None
    if member.last_visit_at:
        last_visit = member.last_visit_at.isoformat()

    return WebMemberRead(
        id=member.id,
        first_name=member.first_name,
        nfc_ids=[n.nfc_id for n in member.nfc_ids],
        last_visit_at=last_visit,
        category=member.category,
        safety_briefing_valid=valid,
        last_safety_briefing=last_sb,
        postal_code=member.postal_code,
        is_present=member.is_present,
    )


def _verify_web_key(provided_key: str | None = Depends(api_key_header)) -> None:
    """Verify that the request uses a valid web, registration or admin key."""
    settings = get_settings()
    valid_keys = []
    if settings.api_key:
        valid_keys.append(settings.api_key)
    if settings.registration_api_key:
        valid_keys.append(settings.registration_api_key)
    if settings.web_api_key:
        valid_keys.append(settings.web_api_key)

    if not provided_key or provided_key not in valid_keys:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Invalid or missing API key",
        )


# ===== LIST members (read-only) =====
@router.get("/members", response_model=list[WebMemberRead])
def web_list_members(
    db: Annotated[Session, Depends(get_db_session)],
    q: str = Query(default="", max_length=256),
    _=Depends(_verify_web_key),
) -> list[WebMemberRead]:
    stmt = select(Member)
    if q:
        term = f"%{q}%"
        stmt = stmt.where(
            Member.first_name.ilike(term)
            | Member.last_name.ilike(term)
            | Member.nfc_ids.any(NfcId.nfc_id.ilike(term))
        )
    members = list(db.scalars(stmt.order_by(Member.id)))
    return [_to_web_read(m) for m in members]


# ===== GET by NFC =====
@router.get("/members/nfc/{nfc_id}", response_model=WebMemberRead)
def web_get_by_nfc(
    nfc_id: str,
    db: Annotated[Session, Depends(get_db_session)],
    _=Depends(_verify_web_key),
) -> WebMemberRead:
    nfc = db.scalar(select(NfcId).where(NfcId.nfc_id == nfc_id.strip().upper()))
    if nfc is None:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail="Member not found",
        )
    member = db.get(Member, nfc.member_id)
    if member is None:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail="Member not found",
        )
    return _to_web_read(member)


# ===== PRESENT =====
@router.get("/members/present", response_model=list[WebMemberRead])
def web_present_members(
    db: Annotated[Session, Depends(get_db_session)],
    _=Depends(_verify_web_key),
) -> list[WebMemberRead]:
    members = list(
        db.scalars(
            select(Member).where(Member.is_present.is_(True)).order_by(Member.id)
        )
    )
    return [_to_web_read(m) for m in members]


# ===== CHECK IN =====
@router.post("/check-in", response_model=PresenceEvent)
def web_check_in(
    req: CheckInRequest,
    db: Annotated[Session, Depends(get_db_session)],
    _=Depends(_verify_web_key),
) -> PresenceEvent:
    from datetime import UTC, datetime

    nfc_id = req.nfc_id.strip().upper()
    nfc = db.scalar(select(NfcId).where(NfcId.nfc_id == nfc_id))
    if nfc is None:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail="Member not found",
        )
    member = db.get(Member, nfc.member_id)
    if member is None:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail="Member not found",
        )

    if member.is_present:
        return PresenceEvent(member=_to_web_read(member), message="Already checked in")

    now = datetime.now(UTC)
    member.is_present = True
    member.current_login_at = now
    member.last_visit_at = now
    member.visits += 1
    db.commit()
    log_check_in(member.id, f"{member.first_name} {member.last_name}", nfc_id)
    return PresenceEvent(member=_to_web_read(member), message="Checked in")


# ===== CHECK OUT =====
@router.post("/check-out", response_model=PresenceEvent)
def web_check_out(
    req: CheckInRequest,
    db: Annotated[Session, Depends(get_db_session)],
    _=Depends(_verify_web_key),
) -> PresenceEvent:
    from datetime import UTC, datetime
    from math import ceil

    nfc_id = req.nfc_id.strip().upper()
    nfc = db.scalar(select(NfcId).where(NfcId.nfc_id == nfc_id))
    if nfc is None:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail="Member not found",
        )
    member = db.get(Member, nfc.member_id)
    if member is None:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail="Member not found",
        )

    if not member.is_present or member.current_login_at is None:
        return PresenceEvent(member=_to_web_read(member), message="Already checked out")

    now = datetime.now(UTC)
    login_at = member.current_login_at
    if login_at.tzinfo is None:
        login_at = login_at.replace(tzinfo=UTC)

    duration = max(1, ceil((now - login_at).total_seconds() / 60))
    member.total_presence_minutes += duration
    member.hours_on_site = member.total_presence_minutes // 60
    member.is_present = False
    member.current_login_at = None
    member.last_visit_at = now
    db.commit()
    log_check_out(
        member.id,
        f"{member.first_name} {member.last_name}",
        nfc_id,
        duration,
    )
    return PresenceEvent(
        member=_to_web_read(member),
        message=f"Checked out, added {duration} minutes",
    )


# ===== CREATE new member (only, no update) =====
@router.post("/members", response_model=WebMemberRead, status_code=201)
def web_create_member(
    member_create: MemberCreate,
    db: Annotated[Session, Depends(get_db_session)],
    _=Depends(_verify_web_key),
) -> WebMemberRead:
    if member_create.category not in {
        "schueler",
        "student",
        "buerger",
        "unternehmen",
        "verein",
        "oeffentlich",
        "mitarbeiter",
    }:
        raise HTTPException(
            status_code=status.HTTP_422_UNPROCESSABLE_ENTITY,
            detail="Invalid category",
        )

    data = member_create.model_dump(
        exclude_none=True, exclude={"safety_briefed", "nfc_ids"}
    )

    from datetime import UTC, datetime

    if member_create.safety_briefed and "last_safety_briefing" not in data:
        data["last_safety_briefing"] = datetime.now(UTC).date()

    member = Member(**data)
    db.add(member)
    db.flush()

    for nfc_id in member_create.nfc_ids:
        clean = nfc_id.strip().upper()
        if clean:
            db.add(NfcId(member_id=member.id, nfc_id=clean))

    try:
        db.commit()
    except IntegrityError as exc:
        db.rollback()
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT,
            detail="NFC ID already registered",
        ) from exc

    db.refresh(member)
    return _to_web_read(member)

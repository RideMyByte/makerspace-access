from datetime import UTC, date, datetime, timedelta
from math import ceil
from typing import Annotated

from app.db import get_db_session
from app.models.member import Member
from app.models.nfc_id import NfcId
from app.schemas.member import (
    BulkSafetyBriefing,
    CheckInRequest,
    MemberCreate,
    MemberRead,
    MemberUpdate,
    PresenceEvent,
    SafetyBriefingUpdate,
)
from app.security.api_key import require_api_key
from fastapi import APIRouter, Depends, HTTPException, Query, status
from sqlalchemy import or_, select
from sqlalchemy.exc import IntegrityError
from sqlalchemy.orm import Session

router = APIRouter(
    prefix="/members",
    tags=["members"],
    dependencies=[Depends(require_api_key)],
)


def _now() -> datetime:
    return datetime.now(UTC)


def _member_to_read(member: Member) -> MemberRead:
    data = {
        "id": member.id,
        "first_name": member.first_name,
        "last_name": member.last_name,
        "email": member.email,
        "postal_code": member.postal_code,
        "birthday": member.birthday,
        "last_safety_briefing": member.last_safety_briefing,
        "is_makerstaff": member.is_makerstaff,
        "category": member.category,
        "nfc_ids": [n.nfc_id for n in member.nfc_ids],
        "created_at": member.created_at,
        "visits": member.visits,
        "hours_on_site": member.hours_on_site,
        "total_presence_minutes": member.total_presence_minutes,
        "is_present": member.is_present,
        "current_login_at": member.current_login_at,
        "last_visit_at": member.last_visit_at,
        "registration_date": member.registration_date,
        "additional_inductions": member.additional_inductions,
    }
    return MemberRead.model_validate(data)


def _find_by_nfc(db: Session, nfc_id: str) -> Member | None:
    nfc = db.scalar(select(NfcId).where(NfcId.nfc_id == nfc_id))
    if nfc is None:
        return None
    return db.get(Member, nfc.member_id)


# ===== CREATE =====
@router.post("", response_model=MemberRead, status_code=status.HTTP_201_CREATED)
def create_member(
    member_create: MemberCreate,
    db: Annotated[Session, Depends(get_db_session)],
) -> MemberRead:
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

    if member_create.safety_briefed and "last_safety_briefing" not in data:
        data["last_safety_briefing"] = _now().date()

    member = Member(**data)
    db.add(member)
    db.flush()

    # Add NFC IDs
    for nfc_id in member_create.nfc_ids:
        nfc_id_clean = nfc_id.strip().upper()
        if nfc_id_clean:
            db.add(NfcId(member_id=member.id, nfc_id=nfc_id_clean))

    try:
        db.commit()
    except IntegrityError as exc:
        db.rollback()
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT,
            detail="NFC ID already registered",
        ) from exc

    db.refresh(member)
    return _member_to_read(member)


# ===== LIST with search + filter =====
@router.get("", response_model=list[MemberRead])
def list_members(
    db: Annotated[Session, Depends(get_db_session)],
    q: str = Query(default="", max_length=256),
    last_visit: str = Query(default="", max_length=64),
) -> list[MemberRead]:
    stmt = select(Member)
    filters = []

    if q:
        try:
            id_val = int(q)
            filters.append(Member.id == id_val)
        except ValueError:
            pass

        term = f"%{q}%"
        filters.append(
            or_(
                Member.last_name.ilike(term),
                Member.first_name.ilike(term),
                Member.nfc_ids.any(NfcId.nfc_id.ilike(term)),
            )
        )

    if last_visit:
        now = _now()
        if last_visit == "week":
            cutoff = now - timedelta(days=7)
        elif last_visit == "month":
            cutoff = now - timedelta(days=30)
        elif last_visit == "halfyear":
            cutoff = now - timedelta(days=180)
        elif last_visit == "year":
            cutoff = now - timedelta(days=365)
        else:
            cutoff = None
        if cutoff:
            filters.append(Member.last_visit_at >= cutoff)

    if filters:
        stmt = stmt.where(*filters)

    members = list(db.scalars(stmt.order_by(Member.id)))
    return [_member_to_read(m) for m in members]


# ===== PRESENT =====
@router.get("/present", response_model=list[MemberRead])
def list_present_members(
    db: Annotated[Session, Depends(get_db_session)],
) -> list[MemberRead]:
    members = list(
        db.scalars(
            select(Member).where(Member.is_present.is_(True)).order_by(Member.id)
        )
    )
    return [_member_to_read(m) for m in members]


# ===== CHECK IN =====
@router.post("/check-in", response_model=PresenceEvent)
def check_in(
    req: CheckInRequest,
    db: Annotated[Session, Depends(get_db_session)],
) -> PresenceEvent:
    nfc_id = req.nfc_id.strip().upper()
    member = _find_by_nfc(db, nfc_id)
    if member is None:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail="Member with this NFC ID not found",
        )

    if member.is_present:
        return PresenceEvent(
            member=_member_to_read(member), message="Already checked in"
        )

    now = _now()
    member.is_present = True
    member.current_login_at = now
    member.last_visit_at = now
    member.visits += 1

    db.commit()
    return PresenceEvent(member=_member_to_read(member), message="Checked in")


# ===== CHECK OUT =====
@router.post("/check-out", response_model=PresenceEvent)
def check_out(
    req: CheckInRequest,
    db: Annotated[Session, Depends(get_db_session)],
) -> PresenceEvent:
    nfc_id = req.nfc_id.strip().upper()
    member = _find_by_nfc(db, nfc_id)
    if member is None:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail="Member with this NFC ID not found",
        )

    if not member.is_present or member.current_login_at is None:
        return PresenceEvent(
            member=_member_to_read(member), message="Already checked out"
        )

    now = _now()
    login_at = member.current_login_at
    if login_at.tzinfo is None:
        login_at = login_at.replace(tzinfo=UTC)

    duration_minutes = max(1, ceil((now - login_at).total_seconds() / 60))
    member.total_presence_minutes += duration_minutes
    member.hours_on_site = member.total_presence_minutes // 60
    member.is_present = False
    member.current_login_at = None
    member.last_visit_at = now

    db.commit()
    return PresenceEvent(
        member=_member_to_read(member),
        message=f"Checked out, added {duration_minutes} minutes",
    )


# ===== GET BY NFC =====
@router.get("/nfc/{nfc_id}", response_model=MemberRead)
def get_by_nfc(
    nfc_id: str,
    db: Annotated[Session, Depends(get_db_session)],
) -> MemberRead:
    member = _find_by_nfc(db, nfc_id.strip().upper())
    if member is None:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail="Member not found",
        )
    return _member_to_read(member)


# ===== UPDATE =====
@router.put("/{member_id}", response_model=MemberRead)
def update_member(
    member_id: int,
    update: MemberUpdate,
    db: Annotated[Session, Depends(get_db_session)],
) -> MemberRead:
    member = db.get(Member, member_id)
    if member is None:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND, detail="Member not found"
        )

    for field, value in update.model_dump(exclude_none=True).items():
        setattr(member, field, value)

    try:
        db.commit()
    except IntegrityError as exc:
        db.rollback()
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT, detail="Database error"
        ) from exc

    return _member_to_read(member)


# ===== ADD NFC ID =====
@router.post("/{member_id}/nfc", response_model=MemberRead)
def add_nfc_id(
    member_id: int,
    req: CheckInRequest,
    db: Annotated[Session, Depends(get_db_session)],
) -> MemberRead:
    member = db.get(Member, member_id)
    if member is None:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND, detail="Member not found"
        )

    nfc_id = req.nfc_id.strip().upper()
    existing = db.scalar(select(NfcId).where(NfcId.nfc_id == nfc_id))
    if existing:
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT,
            detail="NFC ID already registered",
        )

    db.add(NfcId(member_id=member_id, nfc_id=nfc_id))
    db.commit()
    return _member_to_read(member)


# ===== DELETE NFC ID =====
@router.delete("/{member_id}/nfc/{nfc_id}", response_model=MemberRead)
def delete_nfc_id(
    member_id: int,
    nfc_id: str,
    db: Annotated[Session, Depends(get_db_session)],
) -> MemberRead:
    member = db.get(Member, member_id)
    if member is None:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND, detail="Member not found"
        )

    nfc = db.scalar(
        select(NfcId).where(NfcId.member_id == member_id, NfcId.nfc_id == nfc_id)
    )
    if nfc is None:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND, detail="NFC ID not found"
        )

    db.delete(nfc)
    db.commit()
    return _member_to_read(member)


# ===== SAFETY BRIEFING (single) =====
@router.patch("/{member_id}/safety-briefing", response_model=MemberRead)
def update_safety_briefing(
    member_id: int,
    update: SafetyBriefingUpdate,
    db: Annotated[Session, Depends(get_db_session)],
) -> MemberRead:
    member = db.get(Member, member_id)
    if member is None:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND, detail="Member not found"
        )

    member.last_safety_briefing = update.last_safety_briefing
    db.commit()
    return _member_to_read(member)


# ===== SAFETY BRIEFING (bulk) =====
@router.post("/bulk-safety-briefing", response_model=list[MemberRead])
def bulk_safety_briefing(
    bulk: BulkSafetyBriefing,
    db: Annotated[Session, Depends(get_db_session)],
) -> list[MemberRead]:
    members = db.scalars(select(Member).where(Member.id.in_(bulk.member_ids))).all()
    for member in members:
        member.last_safety_briefing = bulk.last_safety_briefing
    db.commit()
    return [_member_to_read(m) for m in members]


# ===== DELETE =====
@router.delete("/{member_id}", status_code=status.HTTP_204_NO_CONTENT)
def delete_member(
    member_id: int,
    db: Annotated[Session, Depends(get_db_session)],
) -> None:
    member = db.get(Member, member_id)
    if member is None:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND, detail="Member not found"
        )

    db.delete(member)
    db.commit()


# ===== GET BY ID =====
@router.get("/{member_id}", response_model=MemberRead)
def get_member(
    member_id: int,
    db: Annotated[Session, Depends(get_db_session)],
) -> MemberRead:
    member = db.get(Member, member_id)
    if member is None:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND, detail="Member not found"
        )
    return _member_to_read(member)

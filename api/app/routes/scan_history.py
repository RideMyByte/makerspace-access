from datetime import UTC, datetime
from typing import Annotated

from app.db import get_db_session
from app.models.scan_event import ScanEvent
from app.security.api_key import require_admin_key
from fastapi import APIRouter, Depends, status
from pydantic import BaseModel, Field
from sqlalchemy import select
from sqlalchemy.orm import Session

router = APIRouter(
    prefix="/scan-history",
    tags=["scan-history"],
    dependencies=[Depends(require_admin_key)],
)


class ScanEntry(BaseModel):
    nfc_id: str
    name: str = ""
    action: str


class ScanEntryCreate(ScanEntry):
    pass


class ScanHistoryList(BaseModel):
    entries: list[ScanEntry]
    timestamps: list[str]


def _now() -> datetime:
    return datetime.now(UTC)


@router.get("", response_model=ScanHistoryList)
def list_scan_history(
    db: Annotated[Session, Depends(get_db_session)],
) -> ScanHistoryList:
    events = list(
        db.scalars(select(ScanEvent).order_by(ScanEvent.created_at.desc()).limit(5))
    )
    entries = [ScanEntry(nfc_id=e.nfc_id, name=e.name, action=e.action) for e in events]
    timestamps = [e.created_at.isoformat() for e in events]
    return ScanHistoryList(entries=entries, timestamps=timestamps)


@router.post("", response_model=ScanEntry, status_code=status.HTTP_201_CREATED)
def create_scan_event(
    event: ScanEntryCreate,
    db: Annotated[Session, Depends(get_db_session)],
) -> ScanEntry:
    db_entry = ScanEvent(nfc_id=event.nfc_id, name=event.name, action=event.action)
    db.add(db_entry)
    db.commit()
    return event


@router.delete("", status_code=status.HTTP_204_NO_CONTENT)
def clear_scan_history(
    db: Annotated[Session, Depends(get_db_session)],
) -> None:
    db.execute(ScanEvent.__table__.delete())
    db.commit()

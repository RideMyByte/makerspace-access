from datetime import UTC, datetime
from threading import Lock
from typing import Annotated

from app.db import get_db_session
from app.models.nfc_id import NfcId
from app.security.api_key import require_api_key
from fastapi import APIRouter, Depends, HTTPException, status
from pydantic import BaseModel, Field
from sqlalchemy import select
from sqlalchemy.orm import Session

router = APIRouter(
    prefix="/pending-nfc",
    tags=["pending-nfc"],
    dependencies=[Depends(require_api_key)],
)

_pending_lock = Lock()
_pending_nfc: list[dict[str, str]] = []


class PendingNfcSubmit(BaseModel):
    nfc_id: str = Field(min_length=1, max_length=128)


class PendingNfcEntry(BaseModel):
    nfc_id: str
    submitted_at: str


def _now() -> datetime:
    return datetime.now(UTC)


@router.post("", response_model=PendingNfcEntry, status_code=status.HTTP_201_CREATED)
def submit_pending_nfc(
    payload: PendingNfcSubmit,
    db: Annotated[Session, Depends(get_db_session)],
) -> PendingNfcEntry:
    existing = db.scalar(select(NfcId).where(NfcId.nfc_id == payload.nfc_id))
    if existing is not None:
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT,
            detail="NFC ID is already registered",
        )

    entry = {
        "nfc_id": payload.nfc_id,
        "submitted_at": _now().isoformat(),
    }
    with _pending_lock:
        _pending_nfc.append(entry)

    return PendingNfcEntry(**entry)


@router.get("", response_model=list[PendingNfcEntry])
def list_pending_nfc() -> list[PendingNfcEntry]:
    with _pending_lock:
        return [PendingNfcEntry(**entry) for entry in _pending_nfc]


@router.delete("/{nfc_id}", status_code=status.HTTP_204_NO_CONTENT)
def delete_pending_nfc(nfc_id: str) -> None:
    global _pending_nfc
    with _pending_lock:
        _pending_nfc = [entry for entry in _pending_nfc if entry["nfc_id"] != nfc_id]

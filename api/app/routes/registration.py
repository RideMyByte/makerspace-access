from typing import Annotated

from app.db import get_db_session
from app.routes.members import _member_to_read, create_member
from app.schemas.member import MemberCreate, MemberRead
from app.security.api_key import require_api_key
from fastapi import APIRouter, Depends
from sqlalchemy.orm import Session

router = APIRouter(
    prefix="/registration",
    tags=["registration"],
    dependencies=[Depends(require_api_key)],
)


@router.post("/members", response_model=MemberRead, status_code=201)
def register_member(
    member_create: MemberCreate,
    db: Annotated[Session, Depends(get_db_session)],
) -> MemberRead:
    """Public registration endpoint. Accepts both admin and registration API keys."""
    return create_member(member_create=member_create, db=db)

from typing import Annotated

from app.db import get_db_session
from app.models.user import User
from app.schemas.user import UserCreate, UserRead
from app.security.api_key import require_api_key
from app.security.passwords import hash_password
from fastapi import APIRouter, Depends, HTTPException, status
from sqlalchemy.exc import IntegrityError
from sqlalchemy.orm import Session

router = APIRouter(
    prefix="/users",
    tags=["users"],
    dependencies=[Depends(require_api_key)],
)


@router.post("", response_model=UserRead, status_code=status.HTTP_201_CREATED)
def create_user(
    user_create: UserCreate,
    db: Annotated[Session, Depends(get_db_session)],
) -> User:
    user_data = user_create.model_dump(exclude={"password"})
    user = User(
        **user_data,
        password_hash=hash_password(user_create.password),
    )
    db.add(user)

    try:
        db.commit()
    except IntegrityError as exc:
        db.rollback()
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT,
            detail="User with this username or email already exists",
        ) from exc

    db.refresh(user)
    return user

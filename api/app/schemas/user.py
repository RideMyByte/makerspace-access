from datetime import datetime
from typing import ClassVar

from pydantic import BaseModel, ConfigDict, EmailStr, Field


class UserBase(BaseModel):
    username: str = Field(min_length=1, max_length=128)
    last_name: str = Field(min_length=1, max_length=255)
    first_name: str = Field(min_length=1, max_length=255)
    email: EmailStr


class UserCreate(UserBase):
    password: str = Field(min_length=12, max_length=256)


class UserRead(UserBase):
    model_config: ClassVar[ConfigDict] = ConfigDict(from_attributes=True)

    id: int
    created_at: datetime

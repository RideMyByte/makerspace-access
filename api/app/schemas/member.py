from datetime import date, datetime, timedelta
from typing import ClassVar

from pydantic import BaseModel, ConfigDict, Field, computed_field

SAFETY_BRIEFING_VALIDITY_DAYS = 180

VALID_CATEGORIES = {
    "schueler",
    "student",
    "buerger",
    "unternehmen",
    "verein",
    "oeffentlich",
    "mitarbeiter",
}


class MemberBase(BaseModel):
    first_name: str = Field(min_length=1, max_length=255)
    last_name: str = Field(min_length=1, max_length=255)
    email: str = Field(default="", max_length=320)
    postal_code: str | None = Field(default=None, max_length=10)
    birthday: date | None = None
    last_safety_briefing: date | None = None
    is_makerstaff: bool = False
    category: str = Field(default="buerger")


class MemberCreate(MemberBase):
    nfc_ids: list[str] = Field(default=[], max_length=10)
    safety_briefed: bool = False


class MemberUpdate(BaseModel):
    first_name: str = Field(min_length=1, max_length=255)
    last_name: str = Field(min_length=1, max_length=255)
    email: str = Field(default="", max_length=320)
    postal_code: str | None = Field(default=None, max_length=10)
    birthday: date | None = None
    last_safety_briefing: date | None = None
    is_makerstaff: bool = False
    category: str = Field(default="buerger")


class MemberRead(MemberBase):
    model_config: ClassVar[ConfigDict] = ConfigDict(from_attributes=True)

    id: int
    nfc_ids: list[str] = Field(default=[])
    created_at: datetime
    hours_on_site: int
    total_presence_minutes: int
    is_present: bool
    current_login_at: datetime | None
    last_visit_at: datetime | None

    @computed_field  # type: ignore[prop-decorator]
    @property
    def safety_briefing_valid(self) -> bool:
        if self.last_safety_briefing is None:
            return False
        return self.last_safety_briefing >= (
            date.today() - timedelta(days=SAFETY_BRIEFING_VALIDITY_DAYS)
        )


class SafetyBriefingUpdate(BaseModel):
    last_safety_briefing: date


class CheckInRequest(BaseModel):
    nfc_id: str = Field(min_length=1, max_length=128)


class PresenceEvent(BaseModel):
    member: MemberRead
    message: str


class BulkSafetyBriefing(BaseModel):
    member_ids: list[int] = Field(min_length=1)
    last_safety_briefing: date

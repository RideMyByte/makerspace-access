from app.models.base import Base
from app.models.nfc_id import NfcId
from sqlalchemy import Boolean, Date, DateTime, Integer, String, func
from sqlalchemy.orm import Mapped, mapped_column, relationship


class Member(Base):
    __tablename__: str = "members"  # type: ignore[assignment]

    id: Mapped[int] = mapped_column(
        Integer, primary_key=True, index=True, autoincrement=True
    )
    first_name: Mapped[str] = mapped_column(String(255), nullable=False)
    last_name: Mapped[str] = mapped_column(String(255), nullable=False)
    email: Mapped[str] = mapped_column(
        String(320), nullable=False, default="", index=True
    )
    postal_code: Mapped[str | None] = mapped_column(String(10), nullable=True)
    birthday: Mapped[Date | None] = mapped_column(Date, nullable=True)
    last_safety_briefing: Mapped[Date | None] = mapped_column(Date, nullable=True)
    is_makerstaff: Mapped[bool] = mapped_column(Boolean, nullable=False, default=False)
    category: Mapped[str] = mapped_column(String(32), nullable=False, default="buerger")
    created_at: Mapped[DateTime] = mapped_column(
        DateTime(timezone=True), server_default=func.now(), nullable=False
    )
    visits: Mapped[int] = mapped_column(Integer, nullable=False, default=0)
    hours_on_site: Mapped[int] = mapped_column(Integer, nullable=False, default=0)
    total_presence_minutes: Mapped[int] = mapped_column(
        Integer, nullable=False, default=0
    )
    is_present: Mapped[bool] = mapped_column(Boolean, nullable=False, default=False)
    current_login_at: Mapped[DateTime | None] = mapped_column(
        DateTime(timezone=True), nullable=True
    )
    last_visit_at: Mapped[DateTime | None] = mapped_column(
        DateTime(timezone=True), nullable=True
    )

    nfc_ids: Mapped[list[NfcId]] = relationship(
        back_populates="member", cascade="all, delete-orphan"
    )

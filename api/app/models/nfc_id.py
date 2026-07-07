from app.models.base import Base
from sqlalchemy import ForeignKey, Integer, String
from sqlalchemy.orm import Mapped, mapped_column, relationship


class NfcId(Base):
    __tablename__: str = "nfc_ids"  # type: ignore[assignment]

    id: Mapped[int] = mapped_column(Integer, primary_key=True, index=True)
    member_id: Mapped[int] = mapped_column(
        Integer,
        ForeignKey("members.id", ondelete="CASCADE"),
        nullable=False,
        index=True,
    )
    nfc_id: Mapped[str] = mapped_column(
        String(128), nullable=False, unique=True, index=True
    )

    member: Mapped["Member"] = relationship(back_populates="nfc_ids")

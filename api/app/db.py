from collections.abc import Generator
from functools import lru_cache

from app.config import get_settings
from app.models import Base
from sqlalchemy import create_engine, inspect, text
from sqlalchemy.engine import Engine
from sqlalchemy.orm import Session, sessionmaker


@lru_cache
def get_engine() -> Engine:
    settings = get_settings()
    return create_engine(settings.sqlalchemy_database_url, pool_pre_ping=True)


@lru_cache
def get_session_factory() -> sessionmaker[Session]:
    return sessionmaker(bind=get_engine(), autoflush=False, autocommit=False)


from collections.abc import Generator
from functools import lru_cache

from app.config import get_settings
from app.models import Base
from sqlalchemy import create_engine, inspect, text
from sqlalchemy.engine import Engine
from sqlalchemy.orm import Session, sessionmaker


@lru_cache
def get_engine() -> Engine:
    settings = get_settings()
    return create_engine(settings.sqlalchemy_database_url, pool_pre_ping=True)


@lru_cache
def get_session_factory() -> sessionmaker[Session]:
    return sessionmaker(bind=get_engine(), autoflush=False, autocommit=False)


def _add_missing_columns() -> None:
    """Add columns to existing tables that are defined in models but not yet in the database."""
    engine = get_engine()
    inspector = inspect(engine)
    table_columns = {}
    for table_name in inspector.get_table_names():
        table_columns[table_name] = {
            col["name"] for col in inspector.get_columns(table_name)
        }

    for table_name, table in Base.metadata.tables.items():
        if table_name not in table_columns:
            continue
        existing = table_columns[table_name]
        for column in table.columns:
            if column.name not in existing:
                col_type = column.type.compile(engine.dialect)
                nullable = "" if column.nullable else " NOT NULL"
                default = ""
                if column.server_default is not None:
                    default = f" DEFAULT {column.server_default.arg}"
                with engine.connect() as conn:
                    conn.execute(
                        text(
                            f'ALTER TABLE "{table_name}" ADD COLUMN "{column.name}" {col_type}{nullable}{default}'
                        )
                    )
                    conn.commit()


def create_database_tables() -> None:
    engine = get_engine()
    inspector = inspect(engine)
    existing_tables = inspector.get_table_names()
    if not existing_tables:
        # Fresh database – create all tables
        Base.metadata.create_all(bind=engine)
    else:
        # Add missing columns to existing tables
        _add_missing_columns()


def get_db_session() -> Generator[Session, None, None]:
    session_factory = get_session_factory()
    with session_factory() as session:
        yield session

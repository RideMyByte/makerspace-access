FROM postgres:16.4-alpine

# Copy initialization scripts into the PostgreSQL init directory.
# These are executed only when the data volume is first created.
COPY init/ /docker-entrypoint-initdb.d/

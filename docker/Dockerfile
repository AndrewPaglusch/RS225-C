FROM alpine:3.20 AS builder

RUN apk add --no-cache gcc musl-dev make

WORKDIR /build
COPY Makefile .
COPY src/ src/

RUN make -j$(nproc)

FROM alpine:3.20

RUN apk add --no-cache libgcc

WORKDIR /app
COPY --from=builder /build/bin/rs225 /app/
COPY data/ /app/data/

EXPOSE 43594

CMD ["./rs225"]

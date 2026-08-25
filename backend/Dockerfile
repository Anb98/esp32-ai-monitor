# Stage 1: Build static binary
FROM golang:1.22-alpine AS builder
WORKDIR /app
COPY go.mod ./
COPY cmd ./cmd
COPY internal ./internal
RUN CGO_ENABLED=0 GOOS=linux go build -ldflags="-s -w" -o server ./cmd/server

# Stage 2: Minimal runtime image (<15MB)
FROM alpine:3.20
RUN apk --no-cache add ca-certificates tzdata
RUN adduser -D -u 1000 appuser
WORKDIR /app
COPY --from=builder /app/server /app/server
USER appuser
EXPOSE 8080
ENV GOMEMLIMIT=8MiB
ENTRYPOINT ["/app/server"]

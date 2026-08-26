package provider_test

import (
	"io"
	"os"
	"path/filepath"
	"testing"
	"time"

	"github.com/esp32-ai-monitor/backend/internal/provider"
)

func copyFixture(t *testing.T, src, dst string) {
	t.Helper()
	in, err := os.Open(src)
	if err != nil {
		t.Fatalf("failed to open fixture %s: %v", src, err)
	}
	defer in.Close()

	out, err := os.Create(dst)
	if err != nil {
		t.Fatalf("failed to create %s: %v", dst, err)
	}
	defer out.Close()

	if _, err := io.Copy(out, in); err != nil {
		t.Fatalf("failed to copy fixture: %v", err)
	}
}

func TestResolveClaudeCreds_ValidCredentials_MsEpochPrecision(t *testing.T) {
	dir := t.TempDir()
	copyFixture(t, "testdata/claude/credentials_valid.json", filepath.Join(dir, ".credentials.json"))

	result := provider.ResolveClaudeCreds(dir, t.TempDir())

	if result.State != provider.CredFound {
		t.Fatalf("expected CredFound, got %v", result.State)
	}
	if result.AccessToken != "sk-ant-oat01-FAKE-TEST-ACCESS-TOKEN-0000000000000000000000000000" {
		t.Errorf("unexpected access token: %s", result.AccessToken)
	}

	// expiresAt in the fixture is 1735689600000 ms == 2025-01-01T00:00:00Z exactly.
	// This proves the ms epoch is divided, not treated as seconds.
	want := time.Date(2025, 1, 1, 0, 0, 0, 0, time.UTC)
	if !result.ExpiresAt.UTC().Equal(want) {
		t.Errorf("expected ExpiresAt %v (ms epoch correctly parsed), got %v", want, result.ExpiresAt.UTC())
	}
}

func TestResolveClaudeCreds_MissingFile_IsAbsentNotUnreadable(t *testing.T) {
	dir := t.TempDir() // empty - no .credentials.json
	result := provider.ResolveClaudeCreds(dir, t.TempDir())

	if result.State != provider.CredAbsent {
		t.Errorf("expected CredAbsent for missing file, got %v", result.State)
	}
}

func TestResolveClaudeCreds_MalformedJSON_IsUnreadableNeverAbsent(t *testing.T) {
	dir := t.TempDir()
	copyFixture(t, "testdata/claude/credentials_malformed.json", filepath.Join(dir, ".credentials.json"))

	result := provider.ResolveClaudeCreds(dir, t.TempDir())

	if result.State != provider.CredUnreadable {
		t.Errorf("expected CredUnreadable for malformed JSON, got %v", result.State)
	}
}

func TestResolveClaudeCreds_TruncatedJSON_IsUnreadable(t *testing.T) {
	dir := t.TempDir()
	copyFixture(t, "testdata/claude/credentials_truncated.json", filepath.Join(dir, ".credentials.json"))

	result := provider.ResolveClaudeCreds(dir, t.TempDir())

	if result.State != provider.CredUnreadable {
		t.Errorf("expected CredUnreadable for truncated JSON, got %v", result.State)
	}
}

func TestResolveClaudeCreds_UnknownLayout_IsUnreadableNeverAbsent(t *testing.T) {
	dir := t.TempDir()
	copyFixture(t, "testdata/claude/credentials_unknown_layout.json", filepath.Join(dir, ".credentials.json"))

	result := provider.ResolveClaudeCreds(dir, t.TempDir())

	if result.State != provider.CredUnreadable {
		t.Errorf("expected CredUnreadable for unknown-layout file (never CredAbsent), got %v", result.State)
	}
}

func TestResolveClaudeCreds_CustomConfigDirPathResolution(t *testing.T) {
	dir := t.TempDir()
	copyFixture(t, "testdata/claude/credentials_valid.json", filepath.Join(dir, ".credentials.json"))

	// Simulates CLAUDE_CONFIG_DIR=<dir> resolving to <dir>/.credentials.json, not <dir>/claude/.credentials.json
	result := provider.ResolveClaudeCreds(dir, t.TempDir())
	if result.State != provider.CredFound {
		t.Fatalf("expected credentials resolved directly under configDir, got %v", result.State)
	}
}

func TestClaudeJSONHasOAuthAccount_StreamedPresent(t *testing.T) {
	home := t.TempDir()
	copyFixture(t, "testdata/claude/claude_json_with_oauth_account.json", filepath.Join(home, ".claude.json"))

	if !provider.ClaudeJSONHasOAuthAccount(home) {
		t.Error("expected oauthAccount to be detected in $HOME/.claude.json")
	}
}

func TestClaudeJSONHasOAuthAccount_StreamedAbsentField(t *testing.T) {
	home := t.TempDir()
	copyFixture(t, "testdata/claude/claude_json_without_oauth_account.json", filepath.Join(home, ".claude.json"))

	if provider.ClaudeJSONHasOAuthAccount(home) {
		t.Error("expected no oauthAccount hint when the field is absent")
	}
}

func TestClaudeJSONHasOAuthAccount_NoFileNeverCrashes(t *testing.T) {
	home := t.TempDir() // no .claude.json at all
	if provider.ClaudeJSONHasOAuthAccount(home) {
		t.Error("expected false when $HOME/.claude.json does not exist")
	}
}

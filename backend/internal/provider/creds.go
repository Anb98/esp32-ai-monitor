package provider

import (
	"encoding/json"
	"io"
	"os"
	"path/filepath"
	"time"
)

// CredState is the tri-state outcome of a credential-file probe.
// Unknown/unparseable layouts MUST resolve to CredUnreadable, never
// CredAbsent, so a mere schema drift is never mistaken for "logged out".
type CredState string

const (
	CredFound      CredState = "found"
	CredAbsent     CredState = "absent"
	CredUnreadable CredState = "unreadable"
)

// CredResult is the parsed outcome of an ordered candidate-file probe.
type CredResult struct {
	State       CredState
	AccessToken string
	// ExpiresAt is zero when the state is not CredFound.
	ExpiresAt time.Time
}

// credFileLimit bounds how many bytes of any single credential/config file we
// will ever read into memory, keeping RSS flat regardless of on-disk size.
const credFileLimit = 64 * 1024

// claudeOAuthFile is the real shape of ~/.claude/.credentials.json.
type claudeOAuthFile struct {
	ClaudeAiOauth struct {
		AccessToken string `json:"accessToken"`
		ExpiresAt   int64  `json:"expiresAt"` // ms epoch
	} `json:"claudeAiOauth"`
}

// ResolveClaudeCreds resolves Claude Code's real on-disk OAuth credentials at
// <claudeConfigDir>/.credentials.json. home is accepted for symmetry with the
// secondary $HOME/.claude.json read (see ClaudeJSONHasOAuthAccount) but is
// not otherwise used by this function.
func ResolveClaudeCreds(claudeConfigDir, home string) CredResult {
	path := filepath.Join(claudeConfigDir, ".credentials.json")
	data, err := readFileLimited(path, credFileLimit)
	if err != nil {
		if os.IsNotExist(err) {
			return CredResult{State: CredAbsent}
		}
		return CredResult{State: CredUnreadable}
	}

	var payload claudeOAuthFile
	if err := json.Unmarshal(data, &payload); err != nil {
		return CredResult{State: CredUnreadable}
	}
	if payload.ClaudeAiOauth.AccessToken == "" {
		// Valid JSON, but not the real claudeAiOauth shape: unknown layout,
		// never asserted as "logged out".
		return CredResult{State: CredUnreadable}
	}

	return CredResult{
		State:       CredFound,
		AccessToken: payload.ClaudeAiOauth.AccessToken,
		ExpiresAt:   time.UnixMilli(payload.ClaudeAiOauth.ExpiresAt),
	}
}

// ClaudeJSONHasOAuthAccount stream-scans $HOME/.claude.json (which can be
// megabytes) for the presence of the "oauthAccount" key, without decoding the
// whole document into memory. It is secondary evidence only and never
// overrides the tri-state result of ResolveClaudeCreds; a read/parse failure
// degrades to false, never a crash.
func ClaudeJSONHasOAuthAccount(homeDir string) bool {
	f, err := os.Open(filepath.Join(homeDir, ".claude.json"))
	if err != nil {
		return false
	}
	defer f.Close()

	// json.Decoder.Token() yields object keys and string values through the
	// same string-typed token, so this can match a value that happens to
	// equal "oauthAccount"; acceptable for a secondary-evidence-only hint.
	dec := json.NewDecoder(io.LimitReader(f, 8*1024*1024))
	for {
		tok, err := dec.Token()
		if err != nil {
			return false
		}
		if key, ok := tok.(string); ok && key == "oauthAccount" {
			return true
		}
	}
}

func readFileLimited(path string, limit int64) ([]byte, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	return io.ReadAll(io.LimitReader(f, limit))
}

func fileMtime(path string) time.Time {
	info, err := os.Stat(path)
	if err != nil {
		return time.Time{}
	}
	return info.ModTime()
}

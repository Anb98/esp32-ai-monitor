package model_test

import (
	"testing"

	"github.com/esp32-ai-monitor/backend/internal/model"
)

func TestProvider_ApplyAuthState_Valid(t *testing.T) {
	p := &model.Provider{}
	p.ApplyAuthState(model.AuthStateValid)

	if p.AuthState != model.AuthStateValid {
		t.Errorf("expected AuthState valid, got %v", p.AuthState)
	}
	if !p.AuthValid {
		t.Error("expected AuthValid=true when AuthState=valid")
	}
	if p.ReLoginRequired {
		t.Error("expected ReLoginRequired=false when AuthState=valid")
	}
}

func TestProvider_ApplyAuthState_Expired(t *testing.T) {
	p := &model.Provider{}
	p.ApplyAuthState(model.AuthStateExpired)

	if p.AuthValid {
		t.Error("expected AuthValid=false when AuthState=expired")
	}
	if !p.ReLoginRequired {
		t.Error("expected ReLoginRequired=true when AuthState=expired")
	}
}

func TestProvider_ApplyAuthState_Unknown(t *testing.T) {
	p := &model.Provider{}
	p.ApplyAuthState(model.AuthStateUnknown)

	if p.AuthValid {
		t.Error("expected AuthValid=false when AuthState=unknown")
	}
	if p.ReLoginRequired {
		t.Error("expected ReLoginRequired=false when AuthState=unknown (never a false re-login)")
	}
}

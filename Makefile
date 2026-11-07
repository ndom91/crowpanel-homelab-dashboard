CONFIG ?= dev.yaml
ESPHOME = uv run esphome

.PHONY: config compile upload logs stub-test check-secrets hooks

config:        ## Validate the YAML
	$(ESPHOME) config $(CONFIG) > /dev/null && echo "config OK"

compile:       ## Full firmware build (local check only; HA add-on deploys)
	$(ESPHOME) compile $(CONFIG)

upload:        ## Build + OTA: make upload DEVICE=<ip> [CONFIG=ha-stub.local.yaml]
	$(ESPHOME) run $(CONFIG) --device $(DEVICE) --no-logs

logs:          ## Stream device logs over the API: make logs DEVICE=<ip>
	$(ESPHOME) logs $(CONFIG) --device $(DEVICE)

# Builds ha-stub.yaml the way the add-on does: in a dir with no repo files, so
# packages come from GitHub and repo-relative paths can't mask bugs.
# Tests what is pushed to main, not local edits. .stub-test/ is gitignored.
stub-test:     ## Compile ha-stub.yaml in an isolated dir (GitHub packages)
	@test -f ha-stub.yaml || (echo "no ha-stub.yaml (copy ha-stub.yaml.example)"; exit 1)
	@mkdir -p .stub-test && cp ha-stub.yaml secrets.yaml .stub-test/
	$(ESPHOME) compile .stub-test/ha-stub.yaml

# Generic checks, plus one regex per line from the gitignored
# .secret-patterns (your LAN domain, subnet, key fragments).
check-secrets: ## Fail if anything secret-looking is staged for commit
	@! git diff --cached --name-only | grep -E '(^|/)secrets\.yaml$$|ha-stub\.yaml$$|\.secret-patterns$$' \
	  || (echo "secrets file staged"; exit 1)
	@! git diff --cached -U0 | grep -E '^\+' | grep -vE '^\+\+\+' \
	  | grep -nE '(key|password|ssid):\s*"[^"$$!]{8,}"|([0-9]{1,3}\.){3}[0-9]{1,3}' \
	  | grep -vE '203\.0\.113\.|192\.0\.2\.|keep-your-generated|your-|change-me|REPLACE_WITH' \
	  || (echo "possible secret/IP in staged diff (above)"; exit 1)
	@if [ -s .secret-patterns ]; then \
	  ! git diff --cached -U0 | grep -E '^\+' | grep -vE '^\+\+\+' | grep -nEf .secret-patterns \
	  || (echo "staged diff matches .secret-patterns (above)"; exit 1); \
	fi
	@echo "check-secrets OK"

hooks:         ## Run check-secrets before every commit
	@printf '#!/bin/sh\nexec make -s check-secrets\n' > .git/hooks/pre-commit
	@chmod +x .git/hooks/pre-commit && echo "pre-commit hook installed"

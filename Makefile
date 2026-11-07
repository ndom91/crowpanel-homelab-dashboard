CONFIG ?= dev.yaml
ESPHOME = uv run esphome

.PHONY: config compile upload logs check-secrets

config:        ## Validate the YAML
	$(ESPHOME) config $(CONFIG) > /dev/null && echo "config OK"

compile:       ## Full firmware build (local check only; HA add-on deploys)
	$(ESPHOME) compile $(CONFIG)

upload:        ## OTA local build: make upload DEVICE=crowpanel.local
	$(ESPHOME) upload $(CONFIG) --device $(DEVICE)

logs:          ## Stream device logs over the API: make logs DEVICE=crowpanel.lan
	$(ESPHOME) logs $(CONFIG) --device $(DEVICE)

check-secrets: ## Fail if anything secret-looking is staged for commit
	@! git diff --cached --name-only | grep -E '(^|/)secrets\.yaml$$|ha-stub\.yaml$$' \
	  || (echo "secrets file staged"; exit 1)
	@! git diff --cached -U0 | grep -E '^\+' | grep -vE '^\+\+\+' \
	  | grep -nE '(key|password|ssid):\s*"[^"$$!]{8,}"|([0-9]{1,3}\.){3}[0-9]{1,3}' \
	  | grep -vE '203\.0\.113\.|192\.0\.2\.|keep-your-generated|your-|change-me|REPLACE_WITH' \
	  || (echo "possible secret/IP in staged diff (above)"; exit 1)
	@echo "check-secrets OK"

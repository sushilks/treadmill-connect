# Treadmill Connect Makefile

PYTHON = python3
PIP = pip3
SRC_DIR = src
MAIN = $(SRC_DIR)/main.py

.PHONY: help install run debug clean

.DEFAULT_GOAL := help

help: ## Show this help message
	@echo "Treadmill Connect - Command Menu"
	@echo "------------------------------"
	@echo "make install      - Install required dependencies"
	@echo "make run          - Start the bridge (Standard Mode)"
	@echo "make debug        - Start the bridge with detailed logs"
	@echo "make verify       - Run direct hardware connection test"
	@echo "make mock         - Start in simulation mode"
	@echo "make esp-build    - Build ESP32 firmware"
	@echo "make esp-flash    - Flash ESP32 firmware (Usage: make esp-flash ENV=esp32-s3-geek)"
	@echo "make esp-monitor  - Monitor ESP32 logs (Usage: make esp-monitor ENV=esp32-s3-geek)"
	@echo "make clean        - Remove temporary files"

install: ## Install python dependencies
	$(PIP) install -r requirements.txt

run: ## Run the bridge
	$(PYTHON) $(MAIN)

debug: ## Run in debug mode (shows all logs)
	$(PYTHON) $(MAIN) --debug

verify: ## Run connection verification tool (No FTMS, Direct Control)
	$(PYTHON) $(SRC_DIR)/direct_connect.py

mock: ## Run in mock mode
	$(PYTHON) $(MAIN) --mock

esp-build: ## Build ESP32 Firmware (Requires PlatformIO)
	cd esp32 && $(PYTHON) -m platformio run $(if $(ENV),-e $(ENV))

esp-flash: ## Flash ESP32 Firmware (Requires PlatformIO)
	cd esp32 && $(PYTHON) -m platformio run --target upload $(if $(ENV),-e $(ENV))

esp-monitor: ## Monitor ESP32 Output
	cd esp32 && $(PYTHON) -m platformio run --target monitor $(if $(ENV),-e $(ENV))

clean: ## Clean up pycache and temp files
	rm -rf __pycache__
	rm -rf $(SRC_DIR)/__pycache__
	rm -rf .pytest_cache
	find . -type f -name "*.pyc" -delete

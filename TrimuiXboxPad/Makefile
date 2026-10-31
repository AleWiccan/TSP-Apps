# Makefile - TrimuiXboxPad
# Se ejecuta DENTRO del contenedor Docker (ver README / preprompt-trimui.md)

APP_NAME  = TrimuiXboxPad
BUILD_DIR = build/$(APP_NAME)
SRC       = src/main.c src/bt_hid.c

CC      = aarch64-linux-gnu-gcc
CFLAGS  = -O2 -Wall -I/usr/include/aarch64-linux-gnu -I/usr/include -D_REENTRANT
LDFLAGS = -L/usr/lib/aarch64-linux-gnu -lSDL2 -lSDL2_ttf -lbluetooth -lm

all: $(BUILD_DIR)/$(APP_NAME) package

$(BUILD_DIR)/$(APP_NAME): $(SRC)
	mkdir -p $(BUILD_DIR)
	$(CC) $(SRC) -o $(BUILD_DIR)/$(APP_NAME) $(CFLAGS) $(LDFLAGS)
	@echo ""
	@echo ">> Verificando arquitectura del binario:"
	file $(BUILD_DIR)/$(APP_NAME)
	@echo ">> glibc maximo exigido por el binario (debe ser <= 2.33):"
	@aarch64-linux-gnu-objdump -T $(BUILD_DIR)/$(APP_NAME) | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1
	@if aarch64-linux-gnu-objdump -T $(BUILD_DIR)/$(APP_NAME) | grep -q -E 'GLIBC_2\.(3[4-9]|[4-9][0-9])'; then echo "ERROR: el binario exige glibc >= 2.34 y no arrancara en la consola"; exit 1; fi

package: $(BUILD_DIR)/$(APP_NAME)
	cp package/launch.sh   $(BUILD_DIR)/launch.sh
	cp package/bt-setup.sh $(BUILD_DIR)/bt-setup.sh
	cp package/bt-teardown.sh $(BUILD_DIR)/bt-teardown.sh
	cp package/bt-autoyes.sh $(BUILD_DIR)/bt-autoyes.sh
	cp package/config.json $(BUILD_DIR)/config.json
	cp package/buttons.ini $(BUILD_DIR)/buttons.ini
	mkdir -p $(BUILD_DIR)/fonts
	@if [ -f package/fonts/font.ttf ]; then cp package/fonts/font.ttf $(BUILD_DIR)/fonts/font.ttf; \
	 else f=$$(ls package/fonts/*.ttf 2>/dev/null | head -n1); \
	      if [ -n "$$f" ]; then cp "$$f" $(BUILD_DIR)/fonts/font.ttf; fi; fi
	@if [ -f package/icon.png ]; then cp package/icon.png $(BUILD_DIR)/icon.png; fi
	chmod +x $(BUILD_DIR)/launch.sh $(BUILD_DIR)/bt-setup.sh $(BUILD_DIR)/bt-teardown.sh $(BUILD_DIR)/bt-autoyes.sh
	@echo ""
	@echo ">> Paquete listo en $(BUILD_DIR)/"
	@if [ ! -f $(BUILD_DIR)/fonts/font.ttf ]; then echo ">> FALTA: pon un .ttf en package/fonts/ (se copia como fonts/font.ttf)"; fi
	@if [ ! -f $(BUILD_DIR)/icon.png ]; then echo ">> AVISO: sin icon.png (ponlo en package/icon.png)"; fi

clean:
	rm -rf build

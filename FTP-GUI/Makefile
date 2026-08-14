APP_NAME   = ftpgui
PAK_NAME   = FTP-GUI
SRC        = src/main.c
BUILD_DIR  = build
OUT_BIN    = $(BUILD_DIR)/$(APP_NAME)

CC      = aarch64-linux-gnu-gcc
CFLAGS  = -O2 -Wall -I/usr/include/aarch64-linux-gnu -I/usr/include -D_REENTRANT
LDFLAGS = -L/usr/lib/aarch64-linux-gnu -lSDL2 -lSDL2_ttf -lm

PKG_DIR = SDCARD/Apps/$(PAK_NAME)

.PHONY: all clean package

all: $(OUT_BIN)
	@file $(OUT_BIN)

$(OUT_BIN): $(SRC)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SRC) -o $(OUT_BIN) $(LDFLAGS)

package: all
	@mkdir -p "$(PKG_DIR)/fonts"
	cp $(OUT_BIN) "$(PKG_DIR)/$(APP_NAME)"
	cp launch.sh "$(PKG_DIR)/launch.sh"
	cp config.json "$(PKG_DIR)/config.json"
	cp icon.png "$(PKG_DIR)/icon.png"
	chmod +x "$(PKG_DIR)/launch.sh" "$(PKG_DIR)/$(APP_NAME)"
	@if [ -f fonts/font.ttf ]; then cp fonts/font.ttf "$(PKG_DIR)/fonts/font.ttf"; \
	else echo "AVISO: no se encontro fonts/font.ttf. Copia una fuente TTF ahi antes de usar la app."; fi
	@echo "Paquete listo en $(PKG_DIR)"

clean:
	rm -rf $(BUILD_DIR) SDCARD

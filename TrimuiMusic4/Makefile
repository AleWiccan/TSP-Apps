APP_NAME = TrimuiMusic
SRC_DIR  = src
BUILD_DIR = build
DIST_DIR = dist

CC      = aarch64-linux-gnu-gcc
FFMPEG_PREFIX = /opt/ffmpeg-aarch64
CFLAGS  = -O2 -Wall -MMD -MP -I/usr/include/aarch64-linux-gnu -I$(FFMPEG_PREFIX)/include -D_REENTRANT
LDFLAGS = -L/usr/lib/aarch64-linux-gnu -L$(FFMPEG_PREFIX)/lib \
          -lSDL2 -lSDL2_ttf \
          -Wl,--start-group -lavformat -lavcodec -lavutil -lswresample -Wl,--end-group \
          -lpthread -lm

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
DEPS = $(OBJS:.o=.d)

.PHONY: all clean package

all: $(BUILD_DIR)/$(APP_NAME)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

-include $(DEPS)

$(BUILD_DIR)/$(APP_NAME): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)
	file $@

package: all
	rm -rf "$(DIST_DIR)/$(APP_NAME).pak"
	mkdir -p "$(DIST_DIR)/$(APP_NAME).pak/fonts"
	mkdir -p "$(DIST_DIR)/$(APP_NAME).pak/lib"
	cp $(BUILD_DIR)/$(APP_NAME) "$(DIST_DIR)/$(APP_NAME).pak/$(APP_NAME)"
	cp launch.sh "$(DIST_DIR)/$(APP_NAME).pak/launch.sh"
	chmod +x "$(DIST_DIR)/$(APP_NAME).pak/launch.sh"
	bash scripts/collect_libs.sh "$(BUILD_DIR)/$(APP_NAME)" "$(DIST_DIR)/$(APP_NAME).pak/lib"
	@echo "-----------------------------------------------------------"
	@echo "Falta copiar manualmente NotoSans-Bold.ttf dentro de:"
	@echo "$(DIST_DIR)/$(APP_NAME).pak/fonts/"
	@echo "-----------------------------------------------------------"

clean:
	rm -rf $(BUILD_DIR) $(DIST_DIR)

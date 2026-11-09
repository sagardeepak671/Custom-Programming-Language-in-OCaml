CXX ?= c++
CPPFLAGS ?= -Iinclude
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
LDFLAGS ?=
LDLIBS ?= -pthread
BUILD_DIR ?= build
TARGET ?= mydsl
PORT ?= 8000
CORE := src/ast.cpp src/lexer.cpp src/parser.cpp src/modules.cpp src/type_checker.cpp src/interpreter.cpp \
        src/debug.cpp src/project.cpp src/server.cpp
OBJECTS := $(CORE:src/%.cpp=$(BUILD_DIR)/%.o) $(BUILD_DIR)/web_page.o
DEPS := $(OBJECTS:.o=.d) $(BUILD_DIR)/main.d $(BUILD_DIR)/tests.d

.PHONY: all run test sanitize serve editor clean
all: $(TARGET)
$(TARGET): $(OBJECTS) $(BUILD_DIR)/main.o
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@
$(BUILD_DIR)/%.o: src/%.cpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
# The browser debugging page and the editor bundle (CodeMirror, built by web/editor into
# web/vendor) are embedded into the binary as C++ raw string literals, so `mydsl --serve` is
# self-contained and needs no network.
$(BUILD_DIR)/web_page.cpp: web/index.html web/vendor/mydsl-editor.js | $(BUILD_DIR)
	{ printf '#include "mydsl.hpp"\nnamespace mydsl {\nextern const char* const kDebugPage; const char* const kDebugPage = R"mydsl_page('; cat web/index.html; printf ')mydsl_page";\n'; \
	  printf 'extern const char* const kEditorScript; const char* const kEditorScript = R"mydsl_page('; cat web/vendor/mydsl-editor.js; printf ')mydsl_page";\n}\n'; } > $@
$(BUILD_DIR)/web_page.o: $(BUILD_DIR)/web_page.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
$(BUILD_DIR)/tests.o: tests/tests.cpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(BUILD_DIR)/tests: $(OBJECTS) $(BUILD_DIR)/tests.o
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@
$(BUILD_DIR):
	mkdir -p $@
run: $(TARGET)
	./$(TARGET) inputs/t.txt
serve: $(TARGET)
	./$(TARGET) --serve $(PORT)
# Rebuilds web/vendor/mydsl-editor.js (CodeMirror + the MyDSL mode, web/editor/src) with npm;
# only needed after editing web/editor/src or upgrading CodeMirror - the result is committed.
editor:
	cd web/editor && npm install --no-audit --no-fund && npm run build
	$(MAKE) $(TARGET)
test: $(TARGET) $(BUILD_DIR)/tests
	./$(BUILD_DIR)/tests
	sh tests/cli.sh ./$(TARGET)
	sh tests/e2e.sh ./$(TARGET)
	sh tests/modules.sh ./$(TARGET)
	sh tests/stdlib.sh ./$(TARGET)
sanitize:
	$(MAKE) test BUILD_DIR=build/sanitize TARGET=build/sanitize/mydsl CXXFLAGS="-std=c++17 -O1 -g -Wall -Wextra -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer" LDFLAGS="-fsanitize=address,undefined"
clean:
	rm -rf build mydsl
-include $(DEPS)

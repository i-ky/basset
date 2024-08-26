CXXFLAGS := -std=c++17 $(CXXFLAGS) $(pkg-config --cflags nlohmann_json)
OBJECTS  := $(patsubst %.cpp,%.o,$(wildcard src/*.cpp))

.PHONY: all clean

all: basset

clean:
	$(RM) $(OBJECTS) basset

basset: $(OBJECTS)
	$(CXX) $(OBJECTS) $(LDFLAGS) $(LIBS) -o $@

OBJECTS:=$(patsubst %.cpp,%.o,$(wildcard src/*.cpp))

basset: $(OBJECTS)
	$(CXX) $(OBJECTS) $(LDFLAGS) $(LIBS) -o $@

all: basset

clean:
	rm -rf $(OBJECTS) basset

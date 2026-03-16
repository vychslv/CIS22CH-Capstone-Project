cmake_minimum_required(VERSION 3.16)
project(AirTravelDatabase VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

find_package(Crow CONFIG REQUIRED)

add_executable(air_travel_app
  src/main.cpp
)

target_compile_definitions(air_travel_app PRIVATE _WIN32_WINNT=0x0601)
target_link_libraries(air_travel_app PRIVATE Crow::Crow)

# Copy data files to build directory so app finds them when run from build
file(COPY ${CMAKE_SOURCE_DIR}/data DESTINATION ${CMAKE_BINARY_DIR})

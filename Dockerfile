FROM mcr.microsoft.com/devcontainers/cpp:0-ubuntu-22.04

# Work inside /app
WORKDIR /app

# Copy the project into the image
COPY . /app

# Ensure vcpkg is available (image already has it, but we set the toolchain path explicitly)
ENV VCPKG_ROOT=/usr/local/vcpkg
ENV VCPKG_FEATURE_FLAGS=manifests

# Install Crow via vcpkg so CMake find_package(Crow CONFIG REQUIRED) works
RUN ${VCPKG_ROOT}/vcpkg install crow

# Configure and build the C++ app in Release mode
RUN mkdir -p build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake && \
    cmake --build . --config Release

# The CMakeLists builds an executable called air_travel_app
WORKDIR /app/build

# The app listens on port 8080
EXPOSE 8080

CMD ["./air_travel_app"]


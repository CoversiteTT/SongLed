cmake_minimum_required(VERSION 3.20)
project(SongLedPcCpp LANGUAGES CXX)

add_executable(SongLedPcCpp WIN32
  main.cpp
)

target_compile_features(SongLedPcCpp PRIVATE cxx_std_17)
target_compile_definitions(SongLedPcCpp PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

target_link_libraries(SongLedPcCpp PRIVATE
  ole32
  oleaut32
  uuid
  setupapi
  shell32
  shlwapi
)

# **ONLY APPLICABLE FOR USER APP**

# This exists to override the default esp32 component in esp-idf
# default esp32 component includes all the linker scripts which aren't required for user build

cmake_minimum_required(VERSION 3.5)

register_component()

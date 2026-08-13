add_library(vthomas_runtime Runtime.cpp)
add_library(vthomas::runtime ALIAS vthomas_runtime)

target_link_libraries(vthomas_runtime PUBLIC vthomas::headers)
target_compile_features(vthomas_runtime PUBLIC cxx_std_20)


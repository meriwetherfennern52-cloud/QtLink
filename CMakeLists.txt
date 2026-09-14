cmake_minimum_required(VERSION 3.16)

project(QtLink LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS
    Core
    Widgets
    Network
    Test
)

find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS
    Core
    Widgets
    Network
    Test
)

enable_testing()

add_subdirectory(common/protocol)
add_subdirectory(client)
add_subdirectory(server)
add_subdirectory(tests)
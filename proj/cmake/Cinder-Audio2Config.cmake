if(NOT TARGET Cinder-Audio2)
    # Define ${Cinder-Audio2_PROJECT_ROOT}. ${CMAKE_CURRENT_LIST_DIR} is just the current directory.
    get_filename_component(Cinder-Audio2_PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

    # Define ${CINDER_PATH} as usual.
    get_filename_component(CINDER_PATH "${Cinder-Audio2_PROJECT_ROOT}/../.." ABSOLUTE)

    #[[# Make a list of source files and define that to be ${SOURCE_LIST}.
    file(GLOB SOURCE_LIST CONFIGURE_DEPENDS
            "${Cinder-Audio2_PROJECT_ROOT}/src/rph/NotificationManager.cpp"
            "${Cinder-Audio2_PROJECT_ROOT}/src/rph/Notification.cpp"
            )

    # Create the library!
    add_library(Cinder-Audio2 ${SOURCE_LIST})]]

    # Add include directories.
    # Notice that `cinderblock.xml` has `<includePath>src</includePath>`.
    # So you need to set `../../src/` to include.
    target_include_directories(Cinder-Audio2 PUBLIC "${Cinder-Audio2_PROJECT_ROOT}/src" )
    target_include_directories(Cinder-Audio2 SYSTEM BEFORE PUBLIC "${CINDER_PATH}/include" )
    target_include_directories(Cinder-Audio2 PRIVATE
            "${Cinder-Audio2_PROJECT_ROOT}/src/cinder"
            "${Cinder-Audio2_PROJECT_ROOT}/src/oggvorbis"
            "${Cinder-Audio2_PROJECT_ROOT}/src/r8brain")


    # If your Cinder block has no source code but instead pre-build libraries,
    # you can specify all of them here (uncomment the below line and adjust to your needs).
    # Make sure to use the libraries for the right platform.
    target_link_libraries(Cinder-Audio2 "${Cinder-OpenCV_PROJECT_ROOT}/lib/macosx/libaudio2.a")

    if(NOT TARGET cinder)
        include("${CINDER_PATH}/proj/cmake/configure.cmake")
        find_package(cinder REQUIRED PATHS
                "${CINDER_PATH}/${CINDER_LIB_DIRECTORY}"
                "$ENV{CINDER_PATH}/${CINDER_LIB_DIRECTORY}")
    endif()
    target_link_libraries(Cinder-Audio2 PRIVATE cinder)

endif()
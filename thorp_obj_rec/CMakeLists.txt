cmake_minimum_required(VERSION 2.8.3)
project(thorp_obj_rec)

find_package(catkin REQUIRED)

catkin_package()

install(DIRECTORY launch
                  config
        DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION})
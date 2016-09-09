
find_package(ECM 1.6.0 CONFIG REQUIRED)
include(KDEInstallDirs)

set(KPACKAGE_RELATIVE_DATA_INSTALL_DIR "kpackage")

# kpackage_install_package(path componentname [root] [install_dir])
#
# Installs a package to the system path
# @arg path The source path to install from, location of metadata.desktop
# @arg componentname The plugin name of the component, corresponding to the
#       X-KDE-PluginInfo-Name key in metadata.desktop
# @arg root The subdirectory to install to, default: packages
# @arg install_dir the path where to install packages, 
#       such as myapp, that would go under prefix/share/myapp:
#       default ${KPACKAGE_RELATIVE_DATA_INSTALL_DIR}
#
# Examples:
# kpackage_install_package(mywidget org.kde.plasma.mywidget plasmoids) # installs an applet
# kpackage_install_package(declarativetoolbox org.kde.toolbox packages) # installs a generic package
# kpackage_install_package(declarativetoolbox org.kde.toolbox) # installs a generic package
#

function(kpackage_install_package dir component)
   set(root ${ARGV2})
   set(install_dir ${ARGV3})
   if(NOT root)
      set(root packages)
   endif()
   if(NOT install_dir)
      set(install_dir ${KPACKAGE_RELATIVE_DATA_INSTALL_DIR})
   endif()
   install(DIRECTORY ${dir}/ DESTINATION ${KDE_INSTALL_DATADIR}/${install_dir}/${root}/${component}
           PATTERN .svn EXCLUDE
           PATTERN CMakeLists.txt EXCLUDE
           PATTERN Messages.sh EXCLUDE
           PATTERN dummydata EXCLUDE)


   get_target_property(kpackagetool_cmd KF5::kpackagetool5 LOCATION)
   set(APPDATAFILE "${CMAKE_CURRENT_BINARY_DIR}/${component}.appdata.xml")
   execute_process(COMMAND ${kpackagetool_cmd} --appstream-metainfo ${CMAKE_CURRENT_SOURCE_DIR}/${dir} OUTPUT_FILE ${APPDATAFILE} ERROR_VARIABLE appstreamerror RESULT_VARIABLE result)
   if(appstreamerror)
        message(WARNING "couldn't generate metainfo for ${component}: ${appstreamerror}")
   else()
        install(FILES ${APPDATAFILE} DESTINATION ${KDE_INSTALL_METAINFODIR})
   endif()

   set(newentry "${kpackagetool_cmd} --generate-index -g -p ${CMAKE_INSTALL_PREFIX}/${KDE_INSTALL_DATADIR}/${install_dir}/${root}\n")
   get_directory_property(currentindex kpackageindex)
   string(FIND "${currentindex}" "${newentry}" alreadyin)
   if (alreadyin LESS 0)
        set(regenerateindex "${currentindex}${newentry}")

        set_directory_properties(PROPERTIES kpackageindex "${regenerateindex}")
        file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/regenerateindex.sh ${regenerateindex})
   endif()
endfunction()




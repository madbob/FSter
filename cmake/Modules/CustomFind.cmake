MACRO(CUSTOM_FIND_PATH ovar)
	find_path( ${ovar}
		${ARGN}
		NO_DEFAULT_PATH
		)
	if(${${ovar}} STREQUAL "${ovar}-NOTFOUND")
		find_path( ${ovar}
			${ARGN}
			)
	endif(${${ovar}} STREQUAL "${ovar}-NOTFOUND")
ENDMACRO(CUSTOM_FIND_PATH)

MACRO(CUSTOM_FIND_LIBRARY ovar)
	find_library( ${ovar}
		${ARGN}
		NO_DEFAULT_PATH
		)
	if(${${ovar}} STREQUAL "${ovar}-NOTFOUND")
		find_library( ${ovar}
			${ARGN}
			)
	endif(${${ovar}} STREQUAL "${ovar}-NOTFOUND")
ENDMACRO(CUSTOM_FIND_LIBRARY)

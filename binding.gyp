{
  "targets": [
    {
      "target_name": "umnode",
      "sources": [
			"src/umnodeabc.cpp",
	   ],
      "include_dirs": [
          "<(module_root_dir)/lib/umlib/include/umimage",
          "<(module_root_dir)/lib/umlib/include/umbase",
          "<(module_root_dir)/lib/hdf5/include",
          "<(module_root_dir)/lib/ilmbase2/include",
          "<(module_root_dir)/lib/ilmbase2/include/Half",
          "<(module_root_dir)/lib/ilmbase2/include/Iex",
          "<(module_root_dir)/lib/ilmbase2/include/IexMath",
          "<(module_root_dir)/lib/ilmbase2/include/IlmThread",
          "<(module_root_dir)/lib/ilmbase2/include/Imath",
          "<(module_root_dir)/lib/ilmbase2/include/OpenEXR",
          "<(module_root_dir)/lib/ilmbase2/include/config.windows",
          "<(module_root_dir)/lib/alembic/include" ],
      "libraries": [
          "<(module_root_dir)/lib/umlib/x64/Release/umimage.lib",
          "<(module_root_dir)/lib/umlib/x64/Release/umbase.lib",
          "<(module_root_dir)/lib/hdf5/x64/Release/libhdf5_hl.lib",
          "<(module_root_dir)/lib/hdf5/x64/Release/libhdf5.lib",
          "<(module_root_dir)/lib/ilmbase2/x64/Release/Imath.lib",
          "<(module_root_dir)/lib/ilmbase2/x64/Release/IlmThread.lib",
          "<(module_root_dir)/lib/ilmbase2/x64/Release/IexMath.lib",
          "<(module_root_dir)/lib/ilmbase2/x64/Release/Iex.lib",
          "<(module_root_dir)/lib/ilmbase2/x64/Release/Half.lib",
          "<(module_root_dir)/lib/alembic/x64/Release/AlembicAbc.lib",
		  "<(module_root_dir)/lib/alembic/x64/Release/AlembicAbcCollection.lib",
		  "<(module_root_dir)/lib/alembic/x64/Release/AlembicAbcCoreAbstract.lib",
		  "<(module_root_dir)/lib/alembic/x64/Release/AlembicAbcCoreFactory.lib",
		  "<(module_root_dir)/lib/alembic/x64/Release/AlembicAbcCoreHDF5.lib",
		  "<(module_root_dir)/lib/alembic/x64/Release/AlembicAbcCoreOgawa.lib",
		  "<(module_root_dir)/lib/alembic/x64/Release/AlembicAbcGeom.lib",
		  "<(module_root_dir)/lib/alembic/x64/Release/AlembicAbcMaterial.lib",
		  "<(module_root_dir)/lib/alembic/x64/Release/AlembicOgawa.lib",
		  "<(module_root_dir)/lib/alembic/x64/Release/AlembicUtil.lib",
       ],
      "configurations": {
            'Debug': {
                'msvs_settings': {
                            'VCCLCompilerTool': {
                                'RuntimeLibrary': '3' # /MDd
                    },
                },
            },
            'Release': {
                'msvs_settings': {
                            'VCCLCompilerTool': {
                                'RuntimeLibrary': '2' # /MD
                    },
                },
            },
        },
    }
  ]
}

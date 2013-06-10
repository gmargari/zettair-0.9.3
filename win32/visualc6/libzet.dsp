# Microsoft Developer Studio Project File - Name="libzet" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Static Library" 0x0104

CFG=libzet - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "libzet.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "libzet.mak" CFG="libzet - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "libzet - Win32 Release" (based on "Win32 (x86) Static Library")
!MESSAGE "libzet - Win32 Debug" (based on "Win32 (x86) Static Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
RSC=rc.exe

!IF  "$(CFG)" == "libzet - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "Release"
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_MBCS" /D "_LIB" /YX /FD /c
# ADD CPP /nologo /W3 /GX /O2 /I "..\..\include" /I "..\..\src\include" /I "..\..\src\include\win32" /I "..\..\src\include\compat" /D "WIN32" /D "NDEBUG" /D "_MBCS" /D "_LIB" /YX /FD /c
# ADD BASE RSC /l 0xc09 /d "NDEBUG"
# ADD RSC /l 0xc09 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LIB32=link.exe -lib
# ADD BASE LIB32 /nologo
# ADD LIB32 /nologo

!ELSEIF  "$(CFG)" == "libzet - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "Debug"
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_MBCS" /D "_LIB" /YX /FD /GZ  /c
# ADD CPP /nologo /W3 /Gm /GX /ZI /Od /I "..\..\include" /I "..\..\src\include" /I "..\..\src\include\win32" /I "..\..\src\include\compat" /D "WIN32" /D "_DEBUG" /D "_MBCS" /D "_LIB" /YX /FD /GZ /c
# ADD BASE RSC /l 0xc09 /d "_DEBUG"
# ADD RSC /l 0xc09 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LIB32=link.exe -lib
# ADD BASE LIB32 /nologo
# ADD LIB32 /nologo

!ENDIF 

# Begin Target

# Name "libzet - Win32 Release"
# Name "libzet - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=..\..\src\alloc.c
# End Source File
# Begin Source File

SOURCE=..\..\src\binsearch.c
# End Source File
# Begin Source File

SOURCE=..\..\src\bit.c
# End Source File
# Begin Source File

SOURCE=..\..\src\btbucket.c
# End Source File
# Begin Source File

SOURCE=..\..\src\btbulk.c
# End Source File
# Begin Source File

SOURCE=..\..\src\bucket.c
# End Source File
# Begin Source File

SOURCE=..\..\src\chash.c
# End Source File
# Begin Source File

SOURCE=..\..\src\cosine.c
# End Source File
# Begin Source File

SOURCE=..\..\src\libtextcodec\crc.c
# End Source File
# Begin Source File

SOURCE=..\..\src\libtextcodec\detectfilter.c
# End Source File
# Begin Source File

SOURCE=..\..\src\dirichlet.c
# End Source File
# Begin Source File

SOURCE=..\..\src\docmap.c
# End Source File
# Begin Source File

SOURCE=..\..\src\error.c
# End Source File
# Begin Source File

SOURCE=..\..\src\fdset.c
# End Source File
# Begin Source File

SOURCE=..\..\src\freemap.c
# End Source File
# Begin Source File

SOURCE=..\..\src\getlongopt.c
# End Source File
# Begin Source File

SOURCE=..\..\src\libtextcodec\gunzipfilter.c
# End Source File
# Begin Source File

SOURCE=..\..\src\hawkapi.c
# End Source File
# Begin Source File

SOURCE=..\..\src\heap.c
# End Source File
# Begin Source File

SOURCE=..\..\src\impact.c
# End Source File
# Begin Source File

SOURCE=..\..\src\impact_build.c
# End Source File
# Begin Source File

SOURCE=..\..\src\index.c
# End Source File
# Begin Source File

SOURCE=..\..\src\index_querybuild.c
# End Source File
# Begin Source File

SOURCE=..\..\src\iobtree.c
# End Source File
# Begin Source File

SOURCE=..\..\src\lcrand.c
# End Source File
# Begin Source File

SOURCE=..\..\src\makeindex.c
# End Source File
# Begin Source File

SOURCE=..\..\src\mem.c
# End Source File
# Begin Source File

SOURCE=..\..\src\merge.c
# End Source File
# Begin Source File

SOURCE=..\..\src\mime.c
# End Source File
# Begin Source File

SOURCE=..\..\src\mlparse.c
# End Source File
# Begin Source File

SOURCE=..\..\src\mlparse_wrap.c
# End Source File
# Begin Source File

SOURCE=..\..\src\objalloc.c
# End Source File
# Begin Source File

SOURCE=..\..\src\okapi_k3.c
# End Source File
# Begin Source File

SOURCE=..\..\src\pcosine.c
# End Source File
# Begin Source File

SOURCE=..\..\src\poolalloc.c
# End Source File
# Begin Source File

SOURCE=..\..\src\postings.c
# End Source File
# Begin Source File

SOURCE=..\..\src\psettings.c
# End Source File
# Begin Source File

SOURCE=..\..\src\psettings_default.c
# End Source File
# Begin Source File

SOURCE=..\..\src\pyramid.c
# End Source File
# Begin Source File

SOURCE=..\..\src\queryparse.c
# End Source File
# Begin Source File

SOURCE=..\..\src\rbtree.c
# End Source File
# Begin Source File

SOURCE=..\..\src\remerge.c
# End Source File
# Begin Source File

SOURCE=..\..\src\reposset.c
# End Source File
# Begin Source File

SOURCE=..\..\src\search.c
# End Source File
# Begin Source File

SOURCE=..\..\src\signals.c
# End Source File
# Begin Source File

SOURCE=..\..\src\stack.c
# End Source File
# Begin Source File

SOURCE=..\..\src\staticalloc.c
# End Source File
# Begin Source File

SOURCE=..\..\src\stem.c
# End Source File
# Begin Source File

SOURCE=..\..\src\stop.c
# End Source File
# Begin Source File

SOURCE=..\..\src\stop_default.c
# End Source File
# Begin Source File

SOURCE=..\..\src\storagep.c
# End Source File
# Begin Source File

SOURCE=..\..\src\str.c
# End Source File
# Begin Source File

SOURCE=..\..\src\libtextcodec\stream.c
# End Source File
# Begin Source File

SOURCE=..\..\src\summarise.c
# End Source File
# Begin Source File

SOURCE=..\..\src\trec_eval.c
# End Source File
# Begin Source File

SOURCE=..\..\src\vec.c
# End Source File
# Begin Source File

SOURCE=..\..\src\vocab.c
# End Source File
# Begin Source File

SOURCE=..\..\src\compat\win32_stubs.c
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# End Group
# Begin Source File

SOURCE=..\..\zlib.lib
# End Source File
# End Target
# End Project

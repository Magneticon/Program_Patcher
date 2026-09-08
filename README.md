# Program_Patcher
Simple program for patching the target Windows NT version of executables.

It only changes the target OS system and subsystem version. Nothing else. If you would want to port a program to older OS, you'll need to load it into the Dependency Walker and track all library calls, which are not resolved in your target system and (and if any) resolve them separately.

This program is helpful, if you target C/C++ programs on MS Visual Studio 2010 on XP, as it targets Win NT 5.01 (Win XP) by default and it is not possible to change it in the Visual Studio itself. You can run Program Patcher on your final x86 executable to set the system flag to some suitable OS, like Windows NT 3.5.

<img width="802" height="158" alt="pp1" src="https://github.com/user-attachments/assets/de1ae09c-cd74-4e9f-a298-feb217534c62" />
<img width="1888" height="1063" alt="pp2" src="https://github.com/user-attachments/assets/e67382c6-9d8a-4719-bdfd-ce737754f55d" />
<img width="1915" height="1066" alt="pp3" src="https://github.com/user-attachments/assets/67dba4fe-3cf6-47e0-a209-207c2450694b" />
<img width="546" height="181" alt="pp4" src="https://github.com/user-attachments/assets/2166860c-51d4-464f-a222-a053d1334137" />

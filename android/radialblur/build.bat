cd jni
call ndk-build
if %ERRORLEVEL% EQU 0 (
	echo ndk-build has failed, build cancelled
	cd..

	mkdir "assets\shaders\base"
	xcopy "..\..\data\shaders\base\*.spv" "assets\shaders\base" /Y

	mkdir "assets\shaders\radialblur"
	xcopy "..\..\data\shaders\radialblur\*.spv" "assets\shaders\radialblur" /Y

	mkdir "assets\models"
	xcopy "..\..\data\models\glowsphere.dae" "assets\models" /Y 

	mkdir "assets\textures"
	xcopy "..\..\data\textures\particle_gradient_rgba.ktx" "assets\textures" /Y 	
	
	mkdir "res\drawable"
	xcopy "..\..\android\images\icon.png" "res\drawable" /Y

	call ant debug -Dout.final.file=vulkanRadialblur.apk
) ELSE (
	echo error : ndk-build failed with errors!
	cd..
)

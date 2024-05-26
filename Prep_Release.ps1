$BuildDate = Get-Date -Format "yyyy.MM.dd.HH.mm.ss"
$CurrentVersion = "v0.1."
$OIMVersionNum = $CurrentVersion + $BuildDate
$Build_FolderPath = ".\Releases\SessionMusicBot_" + $OIMVersionNum
$FMODProj_FolderPath = "\FMOD_Proj"


Function PrepRelease{
	param ($Architecture, $Flavor = "")
	
	# Define Local Parameters
	$Build_TargetPath = $Build_FolderPath + $Architecture + $Flavor
	$FMODProj_TargetPath = $Build_TargetPath + $FMODProj_FolderPath
	
	$Gitignore_DelPath = $FMODProj_TargetPath + "\.gitignore"
	$Cache_DelPath = $FMODProj_TargetPath + "\.cache"
	$Unsaved_DelPath = $FMODProj_TargetPath + "\.unsaved"
	$User_DelPath = $FMODProj_TargetPath + "\.user"
	
	#$Readme_TargetPath = $Build_TargetPath + ""
	
	# Copy main Bot folder to Releases/[versioning + arch]/Bot
	Copy-Item -Path ".\x64\Release" -Destination $Build_TargetPath -Recurse
	
	# Copy FMOD_Proj folder to Releases/[version + arch]/FMOD_Proj
	Copy-Item -Path ".\FMOD_Proj" -Destination $FMODProj_TargetPath -Recurse
	
	# Delete all files/folders in directory starting with "." .gitignore .cache .unsaved .user
	Remove-Item $Gitignore_DelPath
	Remove-Item $Cache_DelPath
	Remove-Item $Unsaved_DelPath
	Remove-Item $User_DelPath
	
	# Copy ReadMe.txt from Releases to Releases/[versioning + arch]
	Copy-Item -Path ".\MyBot\README.txt" -Destination $Build_TargetPath
	
	#Change the build path of the FMOD project
	$path = 'C:\Users\Logan\Desktop\Variable.xml'		#Path to XML file
	$xml = [xml](Get-Content -Path $path) 				#Read contents of XML file
	
	$node = $xml.objects.object.property |				#If the property is of name...
	where {$_.name -eq 'builtBanksOutputDirectory'}
	$node.value = "../Bot/soundbanks"					#Set the value to this.
	$xml.Save($path)									#And save.
	
	#foreach ($property in $xml.objects.object.property) {
	#	if($property.name -eq 'builtBanksOutputDirectory') {
	#		$property.value = "../bot/soundbanks"
	#	}
	#}
}

# For each architecture (x86, x64) and Flavor (Debug, Release)
#x64, Release
PrepRelease("_x64", "_Release")
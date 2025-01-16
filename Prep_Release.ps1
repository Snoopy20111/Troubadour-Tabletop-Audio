cd $PSScriptRoot

$BuildDate = Get-Date -Format "yy.MM.dd.HH.mm.ss"
$CurrentVersion = "v0.1."
$OIMVersionNum = $CurrentVersion + $BuildDate
$Package_FolderPath = ".\Packages\" + $OIMVersionNum + "\SessionMusicBot_" + $OIMVersionNum
$FMODProj_FolderPath = "\FMOD_Proj"

Function PrepRelease {
	param ($Architecture, $BuildLocation, $Flavor)
	
	# Define Local Parameters
	$Package_TargetPath = $Package_FolderPath + $Architecture + $Flavor
	$FMODProj_TargetPath = $Package_TargetPath + $FMODProj_FolderPath
	$FMODWorkspaceXML_Path = $FMODProj_TargetPath + "\Metadata\Workspace.xml"
	
	# Paths of files / folders to delete, easier to be specific than generic
	$Gitignore_DelPath = $FMODProj_TargetPath + "\.gitignore"
	$Cache_DelPath = $FMODProj_TargetPath + "\.cache"
	$Unsaved_DelPath = $FMODProj_TargetPath + "\.unsaved"
	$User_DelPath = $FMODProj_TargetPath + "\.user"
	
	# If the Build Location doesn't exist (or isn't valid) stop here
	if ((Test-Path (Resolve-Path $BuildLocation)) -eq $false) {
		Write-Host "Build Path not found for package of Arch: $Architecture and Flavor: $Flavor"
		return
	}
	
	# Copy main Bot folder to Releases/[versioning + arch]/Bot
	Copy-Item -Path $BuildLocation -Destination $Package_TargetPath -Recurse
	
	# Delete PDB file from Release builds, users won't need that
	if ($Flavor -eq "_Release") {
		Remove-Item "$Package_TargetPath\Troubadour.pdb"
	}
	
	# Copy FMOD_Proj folder to Packages\[version + arch]\FMOD_Proj
	Copy-Item -Path ".\FMOD_Proj" -Destination $FMODProj_TargetPath -Recurse
	
	# Delete files/folders in directory starting with "." .gitignore .cache .unsaved .user
	Remove-Item $Gitignore_DelPath
	if (Test-Path -Path $Cache_DelPath) { Remove-Item $Cache_DelPath -Recurse }
	if (Test-Path -Path $Unsaved_DelPath) { Remove-Item $Unsaved_DelPath -Recurse }
	if (Test-Path -Path $User_DelPath) { Remove-Item $User_DelPath -Recurse }
	
	# Copy ReadMe.txt and Credits.txt from their locations to Packages\[versioning + arch]
	Copy-Item -Path ".\MyBot\Setup Instructions.txt" -Destination $Package_TargetPath
	Copy-Item -Path "Credits.txt" -Destination $Package_TargetPath

    # Delete and re-create new .config files, to avoid the .example nonsense for users
    Remove-Item "$Package_TargetPath\users.config"
    New-Item "$Package_TargetPath\users.config" -ItemType File
    Remove-Item "$Package_TargetPath\token.config"
    New-Item "$Package_TargetPath\token.config" -ItemType File
	
	#Change the build path of the FMOD project
	$xml = [xml](Get-Content -Path $FMODWorkspaceXML_Path) 	#Read contents of XML file
	
	foreach ($property in $xml.objects.object.property) {		# For the property in question
		if($property.name -eq 'builtBanksOutputDirectory') {	# replace the builtBanksOutputDirectory path
			$property.value = "../soundbanks"					# with one appropriate for the packaged program
		}
	}
	$xml.Save((Resolve-Path $FMODWorkspaceXML_Path))		# and then save.
}

# For each architecture (x86, x64) and Flavor (Debug, Release)
PrepRelease "_x64" ".\Builds\x64\Release" "_Release"
#PrepRelease "_x64" ".Builds\x64\Debug" "_Debug"
#PrepRelease "_x86" ".Builds\Win32\Release" "_Release"
#PrepRelease "_x86" ".Builds\Win32\Debug" "_Debug"

$PathOutput = Resolve-Path ".\Packages\$OIMVersionNum"
Write-Host "All Packages prepared! Check $PathOutput for your Packages :)"
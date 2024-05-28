$BuildDate = Get-Date -Format "yy.MM.dd.HH.mm.ss"
$CurrentVersion = "v0.1."
$OIMVersionNum = $CurrentVersion + $BuildDate
$Build_FolderPath = ".\Packages\" + $OIMVersionNum + "\SessionMusicBot_" + $OIMVersionNum
$FMODProj_FolderPath = "\FMOD_Proj"

Function PrepRelease {
	param ($Architecture, $BuildLocation, $Flavor)
	
	# Define Local Parameters
	$Build_TargetPath = $Build_FolderPath + $Architecture + $Flavor
	$FMODProj_TargetPath = $Build_TargetPath + $FMODProj_FolderPath
	$FMODWorkspaceXML_Path = $FMODProj_TargetPath + "\Metadata\Workspace.xml"
	
	$Gitignore_DelPath = $FMODProj_TargetPath + "\.gitignore"
	$Cache_DelPath = $FMODProj_TargetPath + "\.cache"
	$Unsaved_DelPath = $FMODProj_TargetPath + "\.unsaved"
	$User_DelPath = $FMODProj_TargetPath + "\.user"
	
	#Write-Host $Build_TargetPath
	#Write-Host $FMODProj_TargetPath
	#Write-Host $FMODWorkspaceXML_Path
	
	if ((Test-Path (Resolve-Path $BuildLocation)) -eq $false) {
		Write-Host "Build Path not found for package of Arch: $Architecture and Flavor: $Flavor"
		return
	}
	
	# Copy main Bot folder to Releases/[versioning + arch]/Bot
	Copy-Item -Path $BuildLocation -Destination $Build_TargetPath -Recurse
	
	# Delete PDB file, users don't need that
	if ($Flavor -eq "_Release") {
		Remove-Item "$Build_TargetPath\SessionMusicBot.pdb"
	}
	
	# Copy FMOD_Proj folder to Releases/[version + arch]/FMOD_Proj
	Copy-Item -Path ".\FMOD_Proj" -Destination $FMODProj_TargetPath -Recurse
	
	# Delete all files/folders in directory starting with "." .gitignore .cache .unsaved .user
	Remove-Item $Gitignore_DelPath
	Remove-Item $Cache_DelPath -Recurse
	Remove-Item $Unsaved_DelPath -Recurse
	Remove-Item $User_DelPath -Recurse
	
	# Copy ReadMe.txt from Releases to Releases/[versioning + arch]
	Copy-Item -Path ".\MyBot\README.txt" -Destination $Build_TargetPath
	
	#Change the build path of the FMOD project
	$xml = [xml](Get-Content -Path $FMODWorkspaceXML_Path) 	#Read contents of XML file
	
	#$node = $xml.objects.object.property |					#If the property is of name...
	#where {$_.name -eq 'builtBanksOutputDirectory'}
	#$node.value = "../Bot/soundbanks"						#Set the value to this.
	#$xml.Save($FMODWorkspaceXML_Path)										#And save.
	
	foreach ($property in $xml.objects.object.property) {
		if($property.name -eq 'builtBanksOutputDirectory') {
			$property.value = "../soundbanks"
		}
	}
	$xml.Save((Resolve-Path $FMODWorkspaceXML_Path))
}

#Make a folder to hold all the variants
#New-Item -ItemType Directory -Path $Build_FolderPath

# For each architecture (x86, x64) and Flavor (Debug, Release)
PrepRelease "_x64" ".\x64\Release" "_Release"
#PrepRelease "_x64" ".\x64\Debug" "_Debug"
#PrepRelease "_x86" ".\Release" "_Release"
#PrepRelease "_x86" ".\Debug" "_Debug"

$PathOutput = Resolve-Path ".\Packages\$OIMVersionNum"
Write-Host "All Packages prepared! Check $PathOutput for your builds :)"
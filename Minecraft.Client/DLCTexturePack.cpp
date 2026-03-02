#include "stdafx.h"
#include "Common\DLC\DLCGameRulesFile.h"
#include "Common\DLC\DLCGameRulesHeader.h"
#include "Common\DLC\DLCGameRules.h"
#include "DLCTexturePack.h"
#include "Common\DLC\DLCColourTableFile.h"
#include "Common\DLC\DLCUIDataFile.h"
#include "Common\DLC\DLCTextureFile.h"
#include "Common\DLC\DLCLocalisationFile.h"
#include "..\Minecraft.World\StringHelpers.h"
#include "StringTable.h"
#include "Common\DLC\DLCAudioFile.h"

#if defined _XBOX || defined _WINDOWS64
#include "Xbox\XML\ATGXmlParser.h"
#include "Xbox\XML\xmlFilesCallback.h"
#endif

DLCTexturePack::DLCTexturePack(DWORD id, DLCPack *pack, TexturePack *fallback) : AbstractTexturePack(id, NULL, pack->getName(), fallback)
{
	m_dlcInfoPack = pack;
	m_dlcDataPack = NULL;
	bUILoaded = false;
	m_bLoadingData = false;
	m_bHasLoadedData = false;
	m_archiveFile = NULL;
	if (app.getLevelGenerationOptions()) app.getLevelGenerationOptions()->setLoadedData();
	m_bUsingDefaultColourTable = true;

	m_stringTable = NULL;

#ifdef _XBOX
	m_pStreamedWaveBank=NULL;
	m_pSoundBank=NULL;
#endif

	if(m_dlcInfoPack->doesPackContainFile(DLCManager::e_DLCType_LocalisationData, L"languages.loc"))
	{
		DLCLocalisationFile *localisationFile = (DLCLocalisationFile *)m_dlcInfoPack->getFile(DLCManager::e_DLCType_LocalisationData, L"languages.loc");
		m_stringTable = localisationFile->getStringTable();
	}

	// 4J Stu - These calls need to be in the most derived version of the class
	loadIcon();
	loadName();
	loadDescription();
	//loadDefaultHTMLColourTable();
}

void DLCTexturePack::loadIcon()
{
	if(m_dlcInfoPack->doesPackContainFile(DLCManager::e_DLCType_Texture, L"icon.png"))
	{
		DLCTextureFile *textureFile = (DLCTextureFile *)m_dlcInfoPack->getFile(DLCManager::e_DLCType_Texture, L"icon.png");
		m_iconData = textureFile->getData(m_iconSize);
	}
	else
	{
		AbstractTexturePack::loadIcon();
	}
}

void DLCTexturePack::loadComparison()
{
	if(m_dlcInfoPack->doesPackContainFile(DLCManager::e_DLCType_Texture, L"comparison.png"))
	{
		DLCTextureFile *textureFile = (DLCTextureFile *)m_dlcInfoPack->getFile(DLCManager::e_DLCType_Texture, L"comparison.png");
		m_comparisonData = textureFile->getData(m_comparisonSize);
	}
}

void DLCTexturePack::loadName()
{
	texname = L"";

	if(m_dlcInfoPack->GetPackID()&1024)
	{
		if(m_stringTable != NULL)
		{
			texname = m_stringTable->getString(L"IDS_DISPLAY_NAME");
			m_wsWorldName=m_stringTable->getString(L"IDS_WORLD_NAME");
		}
	}
	else
	{	
		if(m_stringTable != NULL)
		{
			texname = m_stringTable->getString(L"IDS_DISPLAY_NAME");
		}
	}

}

void DLCTexturePack::loadDescription()
{
	desc1 = L"";

	if(m_stringTable != NULL)
	{
		desc1 = m_stringTable->getString(L"IDS_TP_DESCRIPTION");
	}
}

wstring DLCTexturePack::getResource(const wstring& name)
{
	// 4J Stu - We should never call this function
#ifndef __CONTENT_PACKAGE
	__debugbreak();
#endif
	return L"";
}

InputStream *DLCTexturePack::getResourceImplementation(const wstring &name) //throws IOException
{
	// 4J Stu - We should never call this function
#ifndef _CONTENT_PACKAGE
	__debugbreak();
	if(hasFile(name)) return NULL;
#endif
	return NULL; //resource;
}

bool DLCTexturePack::hasFile(const wstring &name)
{
	bool hasFile = false;
	if(m_dlcDataPack != NULL) hasFile = m_dlcDataPack->doesPackContainFile(DLCManager::e_DLCType_Texture, name);
	return hasFile;
}

bool DLCTexturePack::isTerrainUpdateCompatible()
{
	return true;
}

wstring DLCTexturePack::getPath(bool bTitleUpdateTexture /*= false*/)
{
	return L"";
}

wstring DLCTexturePack::getAnimationString(const wstring &textureName, const wstring &path)
{
	wstring result = L"";

	wstring fullpath = L"res/" + path + textureName + L".png"; 
	if(hasFile(fullpath))
	{
		result = m_dlcDataPack->getFile(DLCManager::e_DLCType_Texture, fullpath)->getParameterAsString(DLCManager::e_DLCParamType_Anim);
	}

	return result;
}

BufferedImage *DLCTexturePack::getImageResource(const wstring& File, bool filenameHasExtension /*= false*/, bool bTitleUpdateTexture /*=false*/, const wstring &drive /*=L""*/)
{
	if(m_dlcDataPack) return new BufferedImage(m_dlcDataPack, L"/" + File, filenameHasExtension);
	else return fallback->getImageResource(File, filenameHasExtension, bTitleUpdateTexture, drive);
}

DLCPack * DLCTexturePack::getDLCPack()
{
	return m_dlcDataPack;
}

void DLCTexturePack::loadColourTable()
{
	// Load the game colours
	if(m_dlcDataPack != NULL && m_dlcDataPack->doesPackContainFile(DLCManager::e_DLCType_ColourTable, L"colours.col"))
	{
		DLCColourTableFile *colourFile = (DLCColourTableFile *)m_dlcDataPack->getFile(DLCManager::e_DLCType_ColourTable, L"colours.col");
		m_colourTable = colourFile->getColourTable();
		m_bUsingDefaultColourTable = false;
	}
	else
	{
		// 4J Stu - We can delete the default colour table, but not the one from the DLCColourTableFile
		if(!m_bUsingDefaultColourTable) m_colourTable = NULL;
		loadDefaultColourTable();
		m_bUsingDefaultColourTable = true;
	}

	// Load the text colours
#ifdef _XBOX
	if(m_dlcDataPack != NULL && m_dlcDataPack->doesPackContainFile(DLCManager::e_DLCType_UIData, L"TexturePack.xzp"))
	{
		DLCUIDataFile *dataFile = (DLCUIDataFile *)m_dlcDataPack->getFile(DLCManager::e_DLCType_UIData, L"TexturePack.xzp");

		DWORD dwSize = 0;
		PBYTE pbData = dataFile->getData(dwSize);

		const DWORD LOCATOR_SIZE = 256; // Use this to allocate space to hold a ResourceLocator string 
		WCHAR szResourceLocator[ LOCATOR_SIZE ];
		
		// Try and load the HTMLColours.col based off the common XML first, before the deprecated xuiscene_colourtable	
		swprintf(szResourceLocator, LOCATOR_SIZE,L"memory://%08X,%04X#HTMLColours.col",pbData, dwSize);
		BYTE *data;
		UINT dataLength;
		if(XuiResourceLoadAll(szResourceLocator, &data, &dataLength) == S_OK)
		{
			m_colourTable->loadColoursFromData(data,dataLength);

			XuiFree(data);
		}
		else
		{

		swprintf(szResourceLocator, LOCATOR_SIZE,L"memory://%08X,%04X#xuiscene_colourtable.xur",pbData, dwSize);
		HXUIOBJ hScene;
		HRESULT hr = XuiSceneCreate(szResourceLocator,szResourceLocator, NULL, &hScene);

		if(HRESULT_SUCCEEDED(hr))
		{
			loadHTMLColourTableFromXuiScene(hScene);
		}
		else
		{			
			loadDefaultHTMLColourTable();
		}
	}
	}
	else
	{
		loadDefaultHTMLColourTable();
	}
#else
	if(app.hasArchiveFile(L"HTMLColours.col"))
	{
		byteArray textColours = app.getArchiveFile(L"HTMLColours.col");
		m_colourTable->loadColoursFromData(textColours.data,textColours.length);

		delete [] textColours.data;
	}
#endif
}

void DLCTexturePack::loadData()
{
	int mountIndex = m_dlcInfoPack->GetDLCMountIndex();
	app.DebugPrintf("DLCTexturePack::loadData - pack=%s mountIndex=%d fullDataPath=%s\n",
		wstringtofilename(m_dlcInfoPack->getName()),
		mountIndex,
		wstringtofilename(m_dlcInfoPack->getFullDataPath()));

	if(mountIndex > -1)
	{
		DWORD mountResult = ERROR_SUCCESS;
#ifdef _DURANGO
		mountResult = StorageManager.MountInstalledDLC(ProfileManager.GetPrimaryPad(),mountIndex,&DLCTexturePack::packMounted,this,L"TPACK");
#else
		mountResult = StorageManager.MountInstalledDLC(ProfileManager.GetPrimaryPad(),mountIndex,&DLCTexturePack::packMounted,this,"TPACK");
#endif
		if(mountResult!=ERROR_IO_PENDING)
		{
			// corrupt DLC
			m_bHasLoadedData = true;
			if (app.getLevelGenerationOptions()) app.getLevelGenerationOptions()->setLoadedData();
			app.DebugPrintf("DLCTexturePack::loadData - mount failed immediately, pack=%s mountIndex=%d pad=%d ret=%d\n",
				wstringtofilename(m_dlcInfoPack->getName()), mountIndex, ProfileManager.GetPrimaryPad(), mountResult);
		}
		else
		{
			m_bLoadingData = true;
			app.DebugPrintf("DLCTexturePack::loadData - mount pending, pack=%s mountIndex=%d\n",
				wstringtofilename(m_dlcInfoPack->getName()), mountIndex);
		}
	}
	else
	{
	#ifdef _WINDOWS64
		wstring dataFilePath = m_dlcInfoPack->getFullDataPath();
		if(!dataFilePath.empty())
		{
			setHasAudio(false);
			if(m_dlcDataPack == NULL)
			{
				m_dlcDataPack = new DLCPack(m_dlcInfoPack->getName(), 0xffffffff);
			}

			wstring dataFileName = dataFilePath;
			size_t slashIndex = dataFileName.find_last_of(L"/\\");
			if(slashIndex != wstring::npos)
			{
				dataFileName = dataFileName.substr(slashIndex + 1);
			}

			const WCHAR *roots[] =
			{
				L"Windows64Media\\DLC",
				L"DurangoMedia\\DLC",
				L"..\\..\\Minecraft.Client\\Windows64Media\\DLC",
				L"..\\..\\Minecraft.Client\\DurangoMedia\\DLC",
				L"Minecraft.Client\\Windows64Media\\DLC",
				L"Minecraft.Client\\DurangoMedia\\DLC"
			};

			DWORD dwFilesProcessed = 0;
			wstring loadedPackRoot = L"";
			for(unsigned int i = 0; i < _countof(roots); ++i)
			{
				wstring packRoot = wstring(roots[i]) + L"\\" + m_dlcInfoPack->getName();
				wstring candidate = packRoot + L"\\Data\\" + dataFileName;
				File dataFile(candidate);
				if(!dataFile.exists())
				{
					candidate = packRoot + L"\\" + dataFilePath;
					File alternativeDataFile(candidate);
					if(!alternativeDataFile.exists())
					{
						continue;
					}
				}

				if(app.m_dlcManager.readDLCDataFile(dwFilesProcessed, candidate, m_dlcDataPack))
				{
					loadedPackRoot = packRoot;
					app.DebugPrintf("DLCTexturePack::loadData - readDLCDataFile ok, pack=%s root=%s candidate=%s processed=%d\n",
						wstringtofilename(m_dlcInfoPack->getName()),
						wstringtofilename(packRoot),
						wstringtofilename(candidate),
						dwFilesProcessed);
					File archivePath(packRoot + L"\\Data\\media.arc");
					if(!archivePath.exists())
					{
						archivePath = File(packRoot + L"\\media.arc");
					}
					if(archivePath.exists())
					{
						m_archiveFile = new ArchiveFile(archivePath);
						app.DebugPrintf("DLCTexturePack::loadData - archive loaded: %s\n", wstringtofilename(archivePath.getPath()));
					}
					else
					{
						app.DebugPrintf("DLCTexturePack::loadData - no media.arc found under root: %s\n", wstringtofilename(packRoot));
					}
					break;
				}
			}

			if(dwFilesProcessed == 0)
			{
				app.DebugPrintf("DLCTexturePack::loadData - Failed to read DLC data for %s, reverting to default colour table.\n", wstringtofilename(m_dlcInfoPack->getName()));
				delete m_dlcDataPack;
				m_dlcDataPack = NULL;
			}
			else
			{
				DLCPack *pack = m_dlcInfoPack->GetParentPack();
				if(pack == NULL)
				{
					pack = m_dlcInfoPack;
				}

				LevelGenerationOptions *levelGen = app.getLevelGenerationOptions();
				if(!loadedPackRoot.empty())
				{
					app.DebugPrintf("DLCTexturePack::loadData - preparing game rules/base save load: lgo=0x%p loaded=%d ready=%d requiresBaseSave=%d baseSavePath=%s root=%s\n",
						levelGen,
						(levelGen != NULL) ? levelGen->hasLoadedData() : 0,
						(levelGen != NULL) ? levelGen->ready() : 0,
						(levelGen != NULL) ? levelGen->requiresBaseSave() : 0,
						(levelGen != NULL) ? wstringtofilename(levelGen->getBaseSavePath()) : "",
						wstringtofilename(loadedPackRoot));
					int gameRulesCount = pack->getDLCItemsCount(DLCManager::e_DLCType_GameRulesHeader);
					for(int i = 0; i < gameRulesCount; ++i)
					{
						DLCGameRulesHeader *dlcFile = (DLCGameRulesHeader *) pack->getFile(DLCManager::e_DLCType_GameRulesHeader, i);
						if(dlcFile->getGrfPath().empty())
						{
							continue;
						}

						wstring grfPath = dlcFile->getGrfPath();
						wstring grfName = grfPath;
						size_t grfSlashIndex = grfName.find_last_of(L"/\\");
						if(grfSlashIndex != wstring::npos)
						{
							grfName = grfName.substr(grfSlashIndex + 1);
						}

						File grf(loadedPackRoot + L"\\Data\\" + grfPath);
						if(!grf.exists())
						{
							grf = File(loadedPackRoot + L"\\" + grfPath);
						}
						if(!grf.exists())
						{
							grf = File(loadedPackRoot + L"\\Data\\" + grfName);
						}
						if(!grf.exists())
						{
							grf = File(loadedPackRoot + L"\\" + grfName);
						}

						if(grf.exists())
						{
							#ifdef _UNICODE
								wstring path = grf.getPath();
								const WCHAR *pchFilename = path.c_str();
								HANDLE fileHandle = CreateFile(
									pchFilename,
									GENERIC_READ,
									0,
									NULL,
									OPEN_EXISTING,
									FILE_FLAG_SEQUENTIAL_SCAN,
									NULL);
							#else
								const char *pchFilename = wstringtofilename(grf.getPath());
								HANDLE fileHandle = CreateFile(
									pchFilename,
									GENERIC_READ,
									0,
									NULL,
									OPEN_EXISTING,
									FILE_FLAG_SEQUENTIAL_SCAN,
									NULL);
							#endif

							if(fileHandle != INVALID_HANDLE_VALUE)
							{
								DWORD dwFileSize = grf.length();
								DWORD bytesRead = 0;
								PBYTE pbData = (PBYTE)new BYTE[dwFileSize];
								BOOL bSuccess = ReadFile(fileHandle, pbData, dwFileSize, &bytesRead, NULL);
								CloseHandle(fileHandle);
								if(bSuccess)
								{
									dlcFile->setGrfData(pbData, dwFileSize, m_stringTable);
									delete [] pbData;
									app.m_gameRules.setLevelGenerationOptions(dlcFile->lgo);
								}
								else
								{
									delete [] pbData;
								}
							}
							else
							{
								app.DebugPrintf("DLCTexturePack::loadData - Failed to open GRF file: %s\n", wstringtofilename(grf.getPath()));
							}
						}
						else
						{
							app.DebugPrintf("DLCTexturePack::loadData - GRF file not found for path: %s\n", wstringtofilename(grfPath));
						}
					}

					levelGen = app.getLevelGenerationOptions();
					if(levelGen->requiresBaseSave() && !levelGen->getBaseSavePath().empty())
					{
						wstring baseSavePath = levelGen->getBaseSavePath();
						wstring baseSaveName = baseSavePath;
						size_t saveSlashIndex = baseSaveName.find_last_of(L"/\\");
						if(saveSlashIndex != wstring::npos)
						{
							baseSaveName = baseSaveName.substr(saveSlashIndex + 1);
						}

						File grf(loadedPackRoot + L"\\Data\\" + baseSavePath);
						if(!grf.exists())
						{
							grf = File(loadedPackRoot + L"\\" + baseSavePath);
						}
						if(!grf.exists())
						{
							grf = File(loadedPackRoot + L"\\Data\\" + baseSaveName);
						}
						if(!grf.exists())
						{
							grf = File(loadedPackRoot + L"\\" + baseSaveName);
						}

						if(grf.exists())
						{
							#ifdef _UNICODE
								wstring path = grf.getPath();
								const WCHAR *pchFilename = path.c_str();
								HANDLE fileHandle = CreateFile(
									pchFilename,
									GENERIC_READ,
									0,
									NULL,
									OPEN_EXISTING,
									FILE_FLAG_SEQUENTIAL_SCAN,
									NULL);
							#else
								const char *pchFilename = wstringtofilename(grf.getPath());
								HANDLE fileHandle = CreateFile(
									pchFilename,
									GENERIC_READ,
									0,
									NULL,
									OPEN_EXISTING,
									FILE_FLAG_SEQUENTIAL_SCAN,
									NULL);
							#endif

							if(fileHandle != INVALID_HANDLE_VALUE)
							{
								DWORD bytesRead = 0;
								DWORD dwFileSize = GetFileSize(fileHandle, NULL);
								PBYTE pbData = (PBYTE)new BYTE[dwFileSize];
								BOOL bSuccess = ReadFile(fileHandle, pbData, dwFileSize, &bytesRead, NULL);
								CloseHandle(fileHandle);
								if(bSuccess)
								{
									levelGen->setBaseSaveData(pbData, dwFileSize);
									app.DebugPrintf("DLCTexturePack::loadData - base save loaded: path=%s bytes=%d\n",
										wstringtofilename(grf.getPath()),
										dwFileSize);
								}
								else
								{
									delete [] pbData;
								}
							}
							else
							{
								app.DebugPrintf("DLCTexturePack::loadData - Failed to open base save file: %s\n", wstringtofilename(grf.getPath()));
							}
						}
						else
						{
							app.DebugPrintf("DLCTexturePack::loadData - Base save file not found for path: %s\n", wstringtofilename(baseSavePath));
						}
					}

					if(levelGen != NULL)
					{
						DWORD baseSaveSize = 0;
						PBYTE baseSaveData = levelGen->getBaseSaveData(baseSaveSize);
						app.DebugPrintf("DLCTexturePack::loadData - levelGen status pack=%s loaded=%d ready=%d requiresBaseSave=%d baseSavePath=%s hasBaseSaveData=%d baseSaveBytes=%d ptr=0x%p\n",
							wstringtofilename(m_dlcInfoPack->getName()),
							levelGen->hasLoadedData(),
							levelGen->ready(),
							levelGen->requiresBaseSave(),
							wstringtofilename(levelGen->getBaseSavePath()),
							levelGen->hasBaseSaveData(),
							baseSaveSize,
							baseSaveData);
					}
				}

				if(pack->getDLCItemsCount(DLCManager::e_DLCType_Audio) > 0)
				{
					DLCAudioFile *dlcFile = (DLCAudioFile *) pack->getFile(DLCManager::e_DLCType_Audio, 0);
					setHasAudio(true);

					int iOverworldStart = 0;
					int iOverworldC = dlcFile->GetCountofType(DLCAudioFile::e_AudioType_Overworld);
					int iNetherStart = iOverworldC;
					int iNetherC = dlcFile->GetCountofType(DLCAudioFile::e_AudioType_Nether);
					int iEndStart = iOverworldC + iNetherC;
					int iEndC = dlcFile->GetCountofType(DLCAudioFile::e_AudioType_End);

					Minecraft::GetInstance()->soundEngine->SetStreamingSounds(
						iOverworldStart, iOverworldStart + iOverworldC,
						iNetherStart, iNetherStart + iNetherC,
						iEndStart, iEndStart + iEndC,
						iEndStart + iEndC);
				}
			}

			// Keep colour table valid even when DLC data load fails.
			loadColourTable();
		}
		else
		{
			app.DebugPrintf("DLCTexturePack::loadData - empty data path for pack=%s\n", wstringtofilename(m_dlcInfoPack->getName()));
		}
	#endif

		m_bHasLoadedData = true;
		if (app.getLevelGenerationOptions())
		{
			app.getLevelGenerationOptions()->setLoadedData();
			app.DebugPrintf("DLCTexturePack::loadData - setLoadedData called: lgo=0x%p loaded=%d ready=%d requiresBaseSave=%d baseSavePath=%s hasBaseSaveData=%d\n",
				app.getLevelGenerationOptions(),
				app.getLevelGenerationOptions()->hasLoadedData(),
				app.getLevelGenerationOptions()->ready(),
				app.getLevelGenerationOptions()->requiresBaseSave(),
				wstringtofilename(app.getLevelGenerationOptions()->getBaseSavePath()),
				app.getLevelGenerationOptions()->hasBaseSaveData());
		}
		app.DebugPrintf("DLCTexturePack::loadData - finished pack=%s hasData=%d isLoading=%d hasLoadedData=%d\n",
			wstringtofilename(m_dlcInfoPack->getName()),
			hasData(),
			m_bLoadingData,
			m_bHasLoadedData);
		app.SetAction(ProfileManager.GetPrimaryPad(), eAppAction_ReloadTexturePack);
	}
}





wstring DLCTexturePack::getFilePath(DWORD packId, wstring filename, bool bAddDataFolder)
{
	return app.getFilePath(packId,filename,bAddDataFolder);
}

int DLCTexturePack::packMounted(LPVOID pParam,int iPad,DWORD dwErr,DWORD dwLicenceMask)
{
	DLCTexturePack *texturePack = (DLCTexturePack *)pParam;
	texturePack->m_bLoadingData = false;
	if(dwErr!=ERROR_SUCCESS)
	{
		// corrupt DLC
		app.DebugPrintf("Failed to mount DLC for pad %d: %d\n",iPad,dwErr);
	}
	else
	{
		app.DebugPrintf("Mounted DLC for texture pack, attempting to load data\n");
		texturePack->m_dlcDataPack = new DLCPack(texturePack->m_dlcInfoPack->getName(), dwLicenceMask);
		texturePack->setHasAudio(false);
		DWORD dwFilesProcessed = 0;
		// Load the DLC textures
		wstring dataFilePath = texturePack->m_dlcInfoPack->getFullDataPath();
		if(!dataFilePath.empty())
		{
			if(!app.m_dlcManager.readDLCDataFile(dwFilesProcessed, getFilePath(texturePack->m_dlcInfoPack->GetPackID(), dataFilePath),texturePack->m_dlcDataPack))
			{
				delete texturePack->m_dlcDataPack;
				texturePack->m_dlcDataPack = NULL;
			}

			// Load the UI data
			if(texturePack->m_dlcDataPack != NULL)
			{
#ifdef _XBOX
				File xzpPath(getFilePath(texturePack->m_dlcInfoPack->GetPackID(), wstring(L"TexturePack.xzp") ) );

				if(xzpPath.exists())
				{
					const char *pchFilename=wstringtofilename(xzpPath.getPath());
					HANDLE fileHandle = CreateFile(
						pchFilename, // file name
						GENERIC_READ, // access mode
						0, // share mode // TODO 4J Stu - Will we need to share file? Probably not but...
						NULL, // Unused
						OPEN_EXISTING , // how to create // TODO 4J Stu - Assuming that the file already exists if we are opening to read from it
						FILE_FLAG_SEQUENTIAL_SCAN, // file attributes
						NULL // Unsupported
						);

					if( fileHandle != INVALID_HANDLE_VALUE )
					{
						DWORD dwFileSize = xzpPath.length();
						DWORD bytesRead;
						PBYTE pbData =  (PBYTE) new BYTE[dwFileSize];
						BOOL success = ReadFile(fileHandle,pbData,dwFileSize,&bytesRead,NULL);
						CloseHandle(fileHandle);
						if(success)
						{
							DLCUIDataFile *uiDLCFile = (DLCUIDataFile *)texturePack->m_dlcDataPack->addFile(DLCManager::e_DLCType_UIData,L"TexturePack.xzp");
							uiDLCFile->addData(pbData,bytesRead,true);
						
						}
					}
				}
#else
				File archivePath(getFilePath(texturePack->m_dlcInfoPack->GetPackID(), wstring(L"media.arc") ) );
				if(archivePath.exists()) texturePack->m_archiveFile = new ArchiveFile(archivePath);
#endif

				/**
					4J-JEV:
						For all the GameRuleHeader files we find
				*/
				DLCPack *pack = texturePack->m_dlcInfoPack->GetParentPack();
				if(pack == NULL)
				{
					pack = texturePack->m_dlcInfoPack;
				}
				LevelGenerationOptions *levelGen = app.getLevelGenerationOptions();
				if (levelGen != NULL)
				{
					int gameRulesCount = pack->getDLCItemsCount(DLCManager::e_DLCType_GameRulesHeader);
					for(int i = 0; i < gameRulesCount; ++i)
					{
						DLCGameRulesHeader *dlcFile = (DLCGameRulesHeader *) pack->getFile(DLCManager::e_DLCType_GameRulesHeader, i);
					
						if (!dlcFile->getGrfPath().empty())
						{
							File grf( getFilePath(texturePack->m_dlcInfoPack->GetPackID(), dlcFile->getGrfPath() ) );
							if (grf.exists())
							{
#ifdef _UNICODE
								wstring path = grf.getPath();
								const WCHAR *pchFilename=path.c_str();
								HANDLE fileHandle = CreateFile(
									pchFilename, // file name
									GENERIC_READ, // access mode
									0, // share mode // TODO 4J Stu - Will we need to share file? Probably not but...
									NULL, // Unused
									OPEN_EXISTING , // how to create // TODO 4J Stu - Assuming that the file already exists if we are opening to read from it
									FILE_FLAG_SEQUENTIAL_SCAN, // file attributes
									NULL // Unsupported
									);
#else
								const char *pchFilename=wstringtofilename(grf.getPath());
								HANDLE fileHandle = CreateFile(
									pchFilename, // file name
									GENERIC_READ, // access mode
									0, // share mode // TODO 4J Stu - Will we need to share file? Probably not but...
									NULL, // Unused
									OPEN_EXISTING , // how to create // TODO 4J Stu - Assuming that the file already exists if we are opening to read from it
									FILE_FLAG_SEQUENTIAL_SCAN, // file attributes
									NULL // Unsupported
									);
#endif

								if( fileHandle != INVALID_HANDLE_VALUE )
								{
									DWORD dwFileSize = grf.length();
									DWORD bytesRead;
									PBYTE pbData =  (PBYTE) new BYTE[dwFileSize];
									BOOL bSuccess = ReadFile(fileHandle,pbData,dwFileSize,&bytesRead,NULL);
									if(bSuccess==FALSE)
									{
										app.FatalLoadError();
									}
									CloseHandle(fileHandle);

									// 4J-PB - is it possible that we can get here after a read fail and it's not an error?
									dlcFile->setGrfData(pbData, dwFileSize, texturePack->m_stringTable);

									delete [] pbData;

									app.m_gameRules.setLevelGenerationOptions( dlcFile->lgo );
								}
							}
						}
					}
					if(levelGen->requiresBaseSave() && !levelGen->getBaseSavePath().empty() )
					{
						File grf(getFilePath(texturePack->m_dlcInfoPack->GetPackID(), levelGen->getBaseSavePath() ));
						if (grf.exists())
						{
#ifdef _UNICODE
							wstring path = grf.getPath();
							const WCHAR *pchFilename=path.c_str();
							HANDLE fileHandle = CreateFile(
								pchFilename, // file name
								GENERIC_READ, // access mode
								0, // share mode // TODO 4J Stu - Will we need to share file? Probably not but...
								NULL, // Unused
								OPEN_EXISTING , // how to create // TODO 4J Stu - Assuming that the file already exists if we are opening to read from it
								FILE_FLAG_SEQUENTIAL_SCAN, // file attributes
								NULL // Unsupported
								);
#else
							const char *pchFilename=wstringtofilename(grf.getPath());
							HANDLE fileHandle = CreateFile(
								pchFilename, // file name
								GENERIC_READ, // access mode
								0, // share mode // TODO 4J Stu - Will we need to share file? Probably not but...
								NULL, // Unused
								OPEN_EXISTING , // how to create // TODO 4J Stu - Assuming that the file already exists if we are opening to read from it
								FILE_FLAG_SEQUENTIAL_SCAN, // file attributes
								NULL // Unsupported
								);
#endif

							if( fileHandle != INVALID_HANDLE_VALUE )
							{
								DWORD bytesRead,dwFileSize = GetFileSize(fileHandle,NULL);
								PBYTE pbData =  (PBYTE) new BYTE[dwFileSize];
								BOOL bSuccess = ReadFile(fileHandle,pbData,dwFileSize,&bytesRead,NULL);
								if(bSuccess==FALSE)
								{
									app.FatalLoadError();
								}
								CloseHandle(fileHandle);

								// 4J-PB - is it possible that we can get here after a read fail and it's not an error?
								levelGen->setBaseSaveData(pbData, dwFileSize);
							}
						}
					}
				}
				

				// any audio data?
#ifdef _XBOX				
				File audioXSBPath(getFilePath(texturePack->m_dlcInfoPack->GetPackID(), wstring(L"MashUp.xsb") ) );
				File audioXWBPath(getFilePath(texturePack->m_dlcInfoPack->GetPackID(), wstring(L"MashUp.xwb") ) );
				
				if(audioXSBPath.exists() && audioXWBPath.exists())
				{

					texturePack->setHasAudio(true);
					const char *pchXWBFilename=wstringtofilename(audioXWBPath.getPath());
					Minecraft::GetInstance()->soundEngine->CreateStreamingWavebank(pchXWBFilename,&texturePack->m_pStreamedWaveBank);
					const char *pchXSBFilename=wstringtofilename(audioXSBPath.getPath());
					Minecraft::GetInstance()->soundEngine->CreateSoundbank(pchXSBFilename,&texturePack->m_pSoundBank);	

				}
#else 
				//DLCPack *pack = texturePack->m_dlcInfoPack->GetParentPack();
				if(pack->getDLCItemsCount(DLCManager::e_DLCType_Audio)>0)
				{
					DLCAudioFile *dlcFile = (DLCAudioFile *) pack->getFile(DLCManager::e_DLCType_Audio, 0);
					texturePack->setHasAudio(true);
					// init the streaming sound ids for this texture pack
					int iOverworldStart, iNetherStart, iEndStart;
					int iOverworldC, iNetherC, iEndC;

					iOverworldStart=0;
					iOverworldC=dlcFile->GetCountofType(DLCAudioFile::e_AudioType_Overworld);
					iNetherStart=iOverworldC;
					iNetherC=dlcFile->GetCountofType(DLCAudioFile::e_AudioType_Nether);
					iEndStart=iOverworldC+iNetherC;
					iEndC=dlcFile->GetCountofType(DLCAudioFile::e_AudioType_End);

					Minecraft::GetInstance()->soundEngine->SetStreamingSounds(iOverworldStart,iOverworldStart+iOverworldC,
						iNetherStart,iNetherStart+iNetherC,iEndStart,iEndStart+iEndC,iEndStart+iEndC); // push the CD start to after
				}
#endif
}
			texturePack->loadColourTable();
		}

		// 4J-PB - we need to leave the texture pack mounted if it contained streaming audio
		if(texturePack->hasAudio()==false)
		{
#ifdef _XBOX
			StorageManager.UnmountInstalledDLC("TPACK");
#endif
		}
	}
	
	texturePack->m_bHasLoadedData = true;
	if (app.getLevelGenerationOptions()) app.getLevelGenerationOptions()->setLoadedData();
	app.SetAction(ProfileManager.GetPrimaryPad(), eAppAction_ReloadTexturePack);

	return 0;
}

void DLCTexturePack::loadUI()
{
#ifdef _XBOX
//Syntax: "memory://" + Address + "," + Size + "#" + File
//L"memory://0123ABCD,21A3#skin_default.xur"

	// Load new skin
	if(m_dlcDataPack != NULL && m_dlcDataPack->doesPackContainFile(DLCManager::e_DLCType_UIData, L"TexturePack.xzp"))
	{
		DLCUIDataFile *dataFile = (DLCUIDataFile *)m_dlcDataPack->getFile(DLCManager::e_DLCType_UIData, L"TexturePack.xzp");

		DWORD dwSize = 0;
		PBYTE pbData = dataFile->getData(dwSize);

		const DWORD LOCATOR_SIZE = 256; // Use this to allocate space to hold a ResourceLocator string 
		WCHAR szResourceLocator[ LOCATOR_SIZE ];
		swprintf(szResourceLocator, LOCATOR_SIZE,L"memory://%08X,%04X#skin_Minecraft.xur",pbData, dwSize);

		XuiFreeVisuals(L"");


		HRESULT hr = app.LoadSkin(szResourceLocator,NULL);//L"TexturePack");
		if(HRESULT_SUCCEEDED(hr))
		{
			bUILoaded = true;
			//CXuiSceneBase::GetInstance()->SetVisualPrefix(L"TexturePack");
			//CXuiSceneBase::GetInstance()->SkinChanged(CXuiSceneBase::GetInstance()->m_hObj);
		}
	}
#else
	if(m_archiveFile && m_archiveFile->hasFile(L"skin.swf"))
	{
		ui.ReloadSkin();
		bUILoaded = true;
	}
#endif
	else
	{		
		loadDefaultUI();
		bUILoaded = true;
	}

	AbstractTexturePack::loadUI();
#ifndef _XBOX
	if(hasAudio()==false && !ui.IsReloadingSkin())
	{
#ifdef _DURANGO
		StorageManager.UnmountInstalledDLC(L"TPACK");
#else
		StorageManager.UnmountInstalledDLC("TPACK");
#endif
	}
#endif
}

void DLCTexturePack::unloadUI()
{
	// Unload skin
	if(bUILoaded)
	{
#ifdef _XBOX
		XuiFreeVisuals(L"TexturePack");
		XuiFreeVisuals(L"");
		CXuiSceneBase::GetInstance()->SetVisualPrefix(L"");
		CXuiSceneBase::GetInstance()->SkinChanged(CXuiSceneBase::GetInstance()->m_hObj);
#endif
		setHasAudio(false);
	}
	AbstractTexturePack::unloadUI();

	app.m_dlcManager.removePack(m_dlcDataPack);
	m_dlcDataPack = NULL;
	delete m_archiveFile;
	m_bHasLoadedData = false;

	bUILoaded = false;
}

wstring DLCTexturePack::getXuiRootPath()
{
	wstring path = L"";
	if(m_dlcDataPack != NULL && m_dlcDataPack->doesPackContainFile(DLCManager::e_DLCType_UIData, L"TexturePack.xzp"))
	{
		DLCUIDataFile *dataFile = (DLCUIDataFile *)m_dlcDataPack->getFile(DLCManager::e_DLCType_UIData, L"TexturePack.xzp");

		DWORD dwSize = 0;
		PBYTE pbData = dataFile->getData(dwSize);

		const DWORD LOCATOR_SIZE = 256; // Use this to allocate space to hold a ResourceLocator string 
		WCHAR szResourceLocator[ LOCATOR_SIZE ];
		swprintf(szResourceLocator, LOCATOR_SIZE,L"memory://%08X,%04X#",pbData, dwSize);
		path = szResourceLocator;
	}
	return path;
}

unsigned int DLCTexturePack::getDLCParentPackId()
{
	DLCPack *parentPack = m_dlcInfoPack->GetParentPack();
	if(parentPack != NULL)
	{
		return parentPack->GetPackId();
	}
	return m_dlcInfoPack->GetPackId();
}

unsigned char DLCTexturePack::getDLCSubPackId()
{
	return (m_dlcInfoPack->GetPackId()>>24)&0xFF;
}

DLCPack * DLCTexturePack::getDLCInfoParentPack()
{
	DLCPack *parentPack = m_dlcInfoPack->GetParentPack();
	if(parentPack != NULL)
	{
		return parentPack;
	}
	return m_dlcInfoPack;
}

XCONTENTDEVICEID DLCTexturePack::GetDLCDeviceID()
{
	return m_dlcInfoPack->GetDLCDeviceID();
}

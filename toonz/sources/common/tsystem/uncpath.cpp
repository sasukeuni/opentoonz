

#include "tsystem.h"
#include "tconvert.h"

#ifdef WIN32
#define UNICODE // per le funzioni di conversione da/a UNC
#include <windows.h>
#include <lm.h>
#endif

//-----------------------------------------------------------------------------

bool TSystem::isUNC(const TFilePath &path)
{
	// si assume che il path e' gia' in formato UNC se inizia con "\\"
	wstring pathStr = path.getWideString();
	return pathStr.length() > 2 && pathStr.substr(0, 2) == L"\\\\";
}

//------------------------------------------------------------------------------

TFilePath TSystem::toUNC(const TFilePath &fp)
{
#ifdef WIN32
	if (QString::fromStdWString(fp.getWideString()).startsWith('+'))
		return fp;
	if (isUNC(fp))
		return fp;

	string fpStr = toString(fp.getWideString());

	if (fpStr.length() > 1 && fpStr.c_str()[1] == ':') {
		string drive = fpStr.substr(0, 3);
		UINT uiDriveType = GetDriveTypeA(drive.c_str());

		if (uiDriveType & DRIVE_REMOTE) {
			// il drive e' montato

			DWORD cbBuff = _MAX_PATH; // Size of buffer
			char szBuff[_MAX_PATH];   // Buffer to receive information
			REMOTE_NAME_INFO *prni = (REMOTE_NAME_INFO *)&szBuff;

			// Pointers to head of buffer
			UNIVERSAL_NAME_INFO *puni = (UNIVERSAL_NAME_INFO *)&szBuff;

			DWORD dwResult = WNetGetUniversalNameW(toWideString(fpStr).c_str(),
												   UNIVERSAL_NAME_INFO_LEVEL,
												   (LPVOID)&szBuff,
												   &cbBuff);

			switch (dwResult) {
			case NO_ERROR:
				return TFilePath(toString(puni->lpUniversalName));

			case ERROR_NOT_CONNECTED:
				// The network connection does not exists.
				throw TException("The path specified doesn't refer to a shared folder");

			case ERROR_CONNECTION_UNAVAIL:
				// The device is not currently connected,
				// but it is a persistent connection.
				throw TException("The shared folder is not currently connected");

			default:
				throw TException("Cannot convert the path specified to UNC");
			}
		} else {
			//  It's a local drive so search for a share to it...
			NET_API_STATUS res;
			PSHARE_INFO_502 BufPtr, p;

			DWORD er = 0,
				  tr = 0,
				  resume = 0,
				  i;

			int iBestMatch = 0;

			string csTemp,
				csTempDrive,
				csBestMatch;

			do {
				res = NetShareEnum(NULL, 502, (LPBYTE *)&BufPtr, MAX_PREFERRED_LENGTH, &er, &tr, &resume);

				// If the call succeeds,
				if (res == ERROR_SUCCESS || res == ERROR_MORE_DATA) {
					p = BufPtr;

					// Loop through the entries;
					for (i = 1; i <= er; i++) {
						if (p->shi502_type == STYPE_DISKTREE) {
							//#ifdef IS_DOTNET
							// shi502_path e' una wstring, aanche se la dichiarazione di PSHARE_INFO_502 non lo sa!
							wstring shareLocalPathW = (LPWSTR)(p->shi502_path);
							string shareLocalPath = toString(shareLocalPathW);
							//#else
							//string shareLocalPath = toString(p->shi502_path);
							//#endif

							if (toLower(fpStr).find(toLower(shareLocalPath)) == 0) {
								string hostName = TSystem::getHostName().toStdString();
								//   #ifdef IS_DOTNET
								// shi502_netname e' una wstring, anche se la dichiarazione di PSHARE_INFO_502 non lo sa!
								wstring shareNetNameW = (LPWSTR)(p->shi502_netname);
								string shareNetName = toString(shareNetNameW);
								//	 #else
								//string shareNetName = toString(p->shi502_netname);
								//#endif
								shareNetName.append("\\");

								string fp(fpStr);
								string uncpath = "\\\\" + hostName + "\\" +
												 fp.replace(0, shareLocalPath.size(), shareNetName);

								return TFilePath(uncpath);
							}
						}

						p++;
					}

					// Free the allocated buffer.
					NetApiBufferFree(BufPtr);
				} else {
					//TRACE(_T("Error: %ld\n"), res);
				}

				// Continue to call NetShareEnum while
				//  there are more entries.
				//
			} while (res == ERROR_MORE_DATA); // end do
		}
	}

	//throw TException("Cannot convert the path specified to UNC");
	return fp;
#else
	//throw TException("Cannot convert the path specified to UNC");
	return fp;
#endif
}

//-----------------------------------------------------------------------------

TFilePath TSystem::toLocalPath(const TFilePath &fp)
{
#ifdef WIN32
	if (!isUNC(fp))
		return TFilePath(fp);

	string pathStr = toString(fp.getWideString());

	// estrae hostname e il nome dello share dal path UNC
	string::size_type idx = pathStr.find_first_of("\\", 2);
	if (idx == string::npos)
		throw TException("The path specified is not a valid UNC path");

	string hostname = pathStr.substr(2, idx - 2);

	if (toLower(hostname) != toLower(TSystem::getHostName().toStdString()))
		throw TException("The UNC path specified does not refer to the local host");

	string::size_type idx2 = pathStr.find_first_of("\\", idx + 1);
	if (idx2 == string::npos)
		throw TException("The path specified is not a valid UNC path");

	string fpShareName = pathStr.substr(idx + 1, idx2 - idx - 1);
	string path = pathStr.substr(idx2 + 1, pathStr.size() - idx2);

	NET_API_STATUS res;
	do {
		PSHARE_INFO_502 BufPtr, p;

		DWORD er = 0, tr = 0, resume = 0;
		res = NetShareEnum(NULL, 502, (LPBYTE *)&BufPtr, DWORD(-1), &er, &tr, &resume);

		// If the call succeeds,
		if (res == ERROR_SUCCESS || res == ERROR_MORE_DATA) {
			p = BufPtr;

			// Loop through the entries;
			for (int i = 1; i <= (int)er; i++) {
				if (p->shi502_type == STYPE_DISKTREE) {
					//#ifdef IS_DOTNET
					//shi502_netname e' una wstring, anche se la dichiarazione di PSHARE_INFO_502 non lo sa!
					wstring shareNetNameW = (LPWSTR)(p->shi502_netname);
					string shareNetName = toString(shareNetNameW);
					//	#else
					//string shareNetName = toString(p->shi502_netname);
					//#endif

					if (toLower(fpShareName) == toLower(shareNetName)) {
						//#ifdef IS_DOTNET
						// shi502_path e' una wstring, anche se la dichiarazione di PSHARE_INFO_502 non lo sa!
						wstring shareLocalPathW = (LPWSTR)(p->shi502_path);
						string shareLocalPath = toString(shareLocalPathW);
						//#else
						//string shareLocalPath = toString(p->shi502_path);
						//#endif
						string localPath = shareLocalPath + path;
						return TFilePath(localPath);
					}
				}

				p++;
			}

			// Free the allocated buffer.
			NetApiBufferFree(BufPtr);
		} else {
			//TRACE(_T("Error: %ld\n"), res);
		}

		// Continue to call NetShareEnum while
		//  there are more entries.
		//
	} while (res == ERROR_MORE_DATA); // end do

	throw TException("Cannot convert to a local path");

#else
	throw TException("Cannot convert to a local path");
#endif
}

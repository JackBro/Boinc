// this is the "wrapper" file for the zip/unzip functions to expose to BOINC clients

#pragma warning( disable : 4786 )  // Disable warning messages for vector

#ifdef __cplusplus
extern "C" {
#else
extern {
#endif
int unzip_main(int argc, char** argv);
int zip_main(int argc, char** argv);
}

#ifndef _WIN32  // only need for Linux & Mac
#include "config.h"
#include <string>
#include <string.h>
using std::string;
#endif

#include "./unzip/unzip.h"
#include "./zip/zip.h"

#include "boinc_zip.h"
#include "filesys.h" // from BOINC for DirScan
#include <algorithm>

// send in an output filename, advanced options (usually NULL), and numFileIn, szfileIn

#ifndef _MAX_PATH
#define _MAX_PATH 255
#endif

// ZipFileEntry/List stuff
ZipFileEntry::ZipFileEntry(const std::string strIn, unsigned char ucSort)
{
  this->assign(strIn);
  stat(strIn.c_str(), &m_statFile);
  m_ucSort = ucSort;
}

ZipFileEntry::ZipFileEntry(const std::string strIn, const struct stat instat, unsigned char ucSort)
{
  this->assign(strIn);
  m_statFile = instat;
  m_ucSort = ucSort;
}

ZipFileEntry::~ZipFileEntry()
{
  this->assign("");
}

bool ZipFileEntry::operator< (const ZipFileEntry& other) const
{
	bool bRet = false;
	if (m_ucSort & SORT_NAME 
		&& m_ucSort & SORT_ASCENDING
		&& strcmp(this->c_str(), other.c_str())<0)
		bRet = true;
	else if (m_ucSort & SORT_NAME 
		&& m_ucSort & SORT_DESCENDING
		&& strcmp(this->c_str(), other.c_str())>0)
		bRet = true;
	else if (m_ucSort & SORT_TIME 
		&& m_ucSort & SORT_ASCENDING
		&& this->m_statFile.st_mtime < other.m_statFile.st_mtime)
		bRet = true;
	else if (m_ucSort & SORT_TIME 
		&& m_ucSort & SORT_DESCENDING
		&& this->m_statFile.st_mtime > other.m_statFile.st_mtime)
		bRet = true;

  return bRet;
}

int boinc_zip(int bZipType, const std::string szFileZip, const std::string szFileIn)
{
	ZipFileList tempvec;
	tempvec.push_back(ZipFileEntry(szFileIn));
	return boinc_zip(bZipType, szFileZip, &tempvec);
}

int boinc_zip(int bZipType, const char* szFileZip, const char* szFileIn)
{  // just use the regular boinc_zip
	std::string strFileZip, strFileIn;
	strFileZip.assign(szFileZip);
	strFileIn.assign(szFileIn);
	ZipFileList tempvec;
	tempvec.push_back(ZipFileEntry(strFileIn));
	return boinc_zip(bZipType, strFileZip, &tempvec);
}

int boinc_zip(int bZipType, const std::string szFileZip, 
     const ZipFileList* pvectszFileIn)
{
	int carg;
	char** av;
	int iRet = 0, i = 0, nVecSize = 0;

	if (pvectszFileIn) nVecSize = pvectszFileIn->size();

	// if unzipping but no file out, so it just uses cwd, only 3 args
	//if (bZipType == UNZIP_IT)
	//	carg = 3 + nVecSize;
	//else
	carg = 3 + nVecSize;

	// make a dynamic array
	av = (char**) calloc(carg, sizeof(char*));
	for (i=0;i<carg;i++)
		av[i] = (char*) calloc(_MAX_PATH,sizeof(char));

	// just form an argc/argv to spoof the "main"
	// default options are to recurse into directories
	//if (options && strlen(options)) 
	//	strcpy(av[1], options);

	if (bZipType == ZIP_IT)
	{
		strcpy(av[0], "zip");
		// default zip options -- no dir names, no subdirs, highest compression, quiet mode
		if (strlen(av[1])==0)
			strcpy(av[1], "-j9");
			//strcpy(av[1], "-9jq");
		strcpy(av[2], szFileZip.c_str());

		//sz 3 onward will be each vector
		int jj;
		for (jj=0; jj<nVecSize; jj++)
			strcpy(av[3+jj], pvectszFileIn->at(jj).c_str());
	}
	else 
	{
		strcpy(av[0], "unzip");
		// default unzip options -- preserve subdirs, overwrite 
		if (strlen(av[1])==0)
			strcpy(av[1], "-o");
		strcpy(av[2], szFileZip.c_str());

		// if they passed in a directory unzip there
		if (carg == 4)
			sprintf(av[3], "-d%s", pvectszFileIn->at(0).c_str());
	}
	// strcpy(av[carg-1], "");  // null arg	
	// printf("args: %s %s %s %s\n", av[0], av[1], av[2], av[3]);

	if (bZipType == ZIP_IT)
	{
		if (access(szFileZip.c_str(), 0) == 0)
		{ // old zip file exists so unlink (otherwise zip will reuse, doesn't seem to be a flag to 
			// bypass zip reusing it
			unlink(szFileZip.c_str());   
		}
		iRet = zip_main(carg, av);
	}
	else {
		// make sure zip file exists
		if (access(szFileZip.c_str(), 0) == 0)
			iRet = unzip_main(carg, av);
		else
			iRet = 2;   
	}

	for (i=0;i<carg;i++)
		free(av[i]);
	free(av);
	return iRet;
}


// -------------------------------------------------------------------
//
// Function: bool boinc_filelist(const std::string directory
//                                              const std::string pattern,
//                                              ZipFileList* pList)
//
// Description: Supply a directory and a pattern as arguments, along
//              with a FileList variable to hold the result list; sorted by name.
//              Returns a vector of files in the directory which match the
//              pattern.  Returns true for success, or false if there was a problem.
//
// CMC Note: this is a 'BOINC-friendly' implementation of "old" CPDN code
//           the "wildcard matching"/regexp is a bit crude, it matches substrings in 
//           order in a file; to match all files such as *.pc.*.x4.*.nc" you would send in
//			 ".pc|.x4.|.nc" for 'pattern'
//
// --------------------------------------------------------------------

bool boinc_filelist(const std::string directory,
                  const std::string pattern,
                  ZipFileList* pList,
				  const unsigned char ucSort, const bool bClear)
{
	std::string strFile;
        // at most three |'s may be passed in pattern match
	int iPos[3], iFnd, iCtr, i, lastPos;  
	struct stat statF;
	std::string strFullPath;
	char strPart[3][32];
	std::string spattern = pattern;
        std::string strDir = directory;
	std::string strUserDir = directory;
	int iLen = strUserDir.size();

    if (!pList) return false;

	// wildcards are blank!
	if (pattern == "*" || pattern == "*.*") spattern.assign("");

	if (bClear)
		pList->clear();  // removes old entries that may be in pList

	// first tack on a final slash on user dir if required
	if (strUserDir[iLen-1] != '\\' 
		&& strUserDir[iLen] != '/')
	{
		// need a final slash, but what type?
		// / is safe on all OS's for CPDN at least
		// but if they already used \ use that
		// well they didn't use a backslash so just use a slash
		if (strUserDir.find("\\") == -1)
			strUserDir += "/";
		else
			strUserDir += "\\";
	}

	// transform strDir to either all \\ or all /
	int j;
	for (j=0; j<directory.size(); j++)  {
		// take off final / or backslash
		if (j == (directory.size()-1) 
	         && (strDir[j] == '/' || strDir[j]=='\\'))
			strDir.resize(directory.size()-1);
		else {
#ifdef _WIN32  // transform paths appropriate for OS
           if (directory[j] == '/')
				strDir[j] = '\\';
#else
           if (directory[j] == '\\')
				strDir[j] = '/';
#endif
		}
	}

	DirScanner dirscan(strDir);
	memset(strPart, 0x00, 3*32);
	while (dirscan.scan(strFile))
	{			
		iCtr = 0;
		lastPos = 0;
		iPos[0] = -1;
		iPos[1] = -1;
		iPos[2] = -1;
		// match the whole filename returned against the regexp to see if it's a hit
		// first get all the |'s to get the pieces to verify
		while (iCtr<3 && (iPos[iCtr] = (int) spattern.find('|', lastPos)) > -1)
		{
			if (iCtr==0)  {
				strncpy(strPart[0], spattern.c_str(), iPos[iCtr]);
			}
			else  {
				strncpy(strPart[iCtr], spattern.c_str()+lastPos, iPos[iCtr]-lastPos);
			}
			lastPos = iPos[iCtr]+1;

			iCtr++;
		}
		if (iCtr>0)  // found a | so need to get the part from lastpos onward
		{
			strncpy(strPart[iCtr], spattern.c_str()+lastPos, spattern.length() - lastPos);
		}

		// check no | were found at all
		if (iCtr == 0)
		{
			strcpy(strPart[0], spattern.c_str());
			iCtr++; // fake iCtr up 1 to get in the loop below
		}

		bool bFound = true;
		for (i = 0; i <= iCtr && bFound; i++)
		{
			if (i==0)  {
				iFnd = (int) strFile.find(strPart[0]);
				bFound = (bool) (iFnd > -1);
			}
			else  {
				// search forward of the old part found
				iFnd = (int) strFile.find(strPart[i], iFnd+1);
				bFound = bFound && (bool) (iFnd > -1);
			}
		}

		if (bFound)
		{
			// this pattern matched the file, add to vector
			// NB: first get stat to make sure it really is a file
			strFullPath = strUserDir + strFile;
			// only add if the file really exists (i.e. not a directory)
            if (is_file(strFullPath.c_str())) {
				ZipFileEntry zfe(strFullPath, statF, ucSort);
				pList->push_back(zfe);
			}
		}

	}

	// sort by file creation time

    std::sort(pList->begin(), pList->end());  // may as well sort it?
    return true;
}

const char *BOINC_RCSID_bdf38b2dfb = "$Id$";

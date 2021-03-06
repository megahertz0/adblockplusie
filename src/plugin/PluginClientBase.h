/*
 * This file is part of Adblock Plus <https://adblockplus.org/>,
 * Copyright (C) 2006-2015 Eyeo GmbH
 *
 * Adblock Plus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * Adblock Plus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Adblock Plus.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _PLUGIN_CLIENT_BASE_H_
#define _PLUGIN_CLIENT_BASE_H_


#include "PluginTypedef.h"


class CPluginClientFactory;


class CPluginError
{

private:

  int m_errorId;
  int m_errorSubid;
  DWORD m_errorCode;
  CString m_errorDescription;
  DWORD m_processId;
  DWORD m_threadId;

public:

  CPluginError(int errorId, int errorSubid, DWORD errorCode, const CString& errorDesc) : 
    m_errorId(errorId), m_errorSubid(errorSubid), m_errorCode(errorCode), m_errorDescription(errorDesc)
  {
    m_processId = ::GetCurrentProcessId();
    m_threadId = ::GetCurrentThreadId();
  }

  CPluginError() : 
    m_errorId(0), m_errorSubid(0), m_errorCode(0), m_processId(0), m_threadId(0) {}

  CPluginError(const CPluginError& org) : 
    m_errorId(org.m_errorId), m_errorSubid(org.m_errorSubid), m_errorCode(org.m_errorCode), m_errorDescription(org.m_errorDescription), m_processId(org.m_processId), m_threadId(org.m_threadId) {}

  int GetErrorId() const { return m_errorId; }
  int GetErrorSubid() const { return m_errorSubid; }
  DWORD GetErrorCode() const { return m_errorCode; }
  CString GetErrorDescription() const { return m_errorDescription; }
  DWORD GetProcessId() const { return m_processId; }
  DWORD GetThreadId() const { return m_threadId; }
};


class CPluginClientBase
{
  friend class CPluginClientFactory;

private:

  static std::vector<CPluginError> s_pluginErrors;

  static bool s_isErrorLogging;

protected:

  // Protected constructor used by the singleton pattern
  CPluginClientBase();

  static CComAutoCriticalSection s_criticalSectionLocal;

public:

  ~CPluginClientBase();

  static void SetLocalization();

  static void LogPluginError(DWORD errorCode, int errorId, int errorSubid, const CString& description="", bool isAsync=false, DWORD dwProcessId=0, DWORD dwThreadId=0);

  static void PostPluginError(int errorId, int errorSubid, DWORD errorCode, const CString& errorDescription);
  static bool PopFirstPluginError(CPluginError& pluginError);
};

/**
 * Wrapper around Microsoft API 'UrlUnescape'
 *
 * This function has modify-in-place semantics.
 * This behavior matches that of the legacy version of this function declared above.
 * At present, callers of this function have no code to handle error conditions that might arise here.
 * Because there's no error handling, therefore, this masks failures in UrlUnescape.
 */
void UnescapeUrl(std::wstring& url);

#endif // _PLUGIN_CLIENT_BASE_H_

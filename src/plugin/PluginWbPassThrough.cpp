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

#include "PluginStdAfx.h"

#include "PluginWbPassThrough.h"
#include "PluginClient.h"
#include "PluginClientFactory.h"
#include "PluginFilter.h"
#include "PluginSettings.h"
#include "PluginClass.h"
#include "PluginSystem.h"
#include <WinInet.h>
#include "wtypes.h"
#include "../shared/Utils.h"
#include "../shared/IE_version.h"

namespace
{
  const std::string g_blockedByABPPage = "<!DOCTYPE html>"
    "<html>"
        "<body>"
          "<!-- blocked by AdblockPlus -->"
        "</body>"
    "</html>";

  template <class T>
  T ExtractHttpHeader(const T& allHeaders, const T& targetHeaderNameWithColon, const T& delimiter)
  {
    auto targetHeaderBeginsAt = allHeaders.find(targetHeaderNameWithColon);
    if (targetHeaderBeginsAt == T::npos)
    {
      return T();
    }
    targetHeaderBeginsAt += targetHeaderNameWithColon.length();
    auto targetHeaderEndsAt = allHeaders.find(delimiter, targetHeaderBeginsAt);
    if (targetHeaderEndsAt == T::npos)
    {
      return T();
    }
    return allHeaders.substr(targetHeaderBeginsAt, targetHeaderEndsAt - targetHeaderBeginsAt);
  }

  std::string ExtractHttpAcceptHeader(IInternetProtocol* internetProtocol)
  {
    // Despite there being HTTP_QUERY_ACCEPT and other query info flags, they don't work here,
    // only HTTP_QUERY_RAW_HEADERS_CRLF | HTTP_QUERY_FLAG_REQUEST_HEADERS does work.
    ATL::CComPtr<IWinInetHttpInfo> winInetHttpInfo;
    HRESULT hr = internetProtocol->QueryInterface(&winInetHttpInfo);
    if (FAILED(hr) || !winInetHttpInfo)
    {
      return "";
    }
    DWORD size = 0;
    DWORD flags = 0;
    DWORD queryOption = HTTP_QUERY_RAW_HEADERS_CRLF | HTTP_QUERY_FLAG_REQUEST_HEADERS;
    hr = winInetHttpInfo->QueryInfo(queryOption, /*buffer*/ nullptr, /*get size*/ &size, &flags, /*reserved*/ 0);
    if (FAILED(hr))
    {
      return "";
    }
    std::string buf(size, '\0');
    hr = winInetHttpInfo->QueryInfo(queryOption, &buf[0], &size, &flags, 0);
    if (FAILED(hr))
    {
      return "";
    }
    return ExtractHttpHeader<std::string>(buf, "Accept:", "\r\n");
  }

  bool IsXmlHttpRequest(const std::wstring& additionalHeaders)
  {
    auto requestedWithHeader = ExtractHttpHeader<std::wstring>(additionalHeaders, L"X-Requested-With:", L"\n");
    return TrimString(requestedWithHeader) == L"XMLHttpRequest";
  }
}

WBPassthruSink::WBPassthruSink()
  : m_currentPositionOfSentPage(0)
  , m_contentType(CFilter::EContentType::contentTypeAny)
  , m_isCustomResponse(false)
{
}

int WBPassthruSink::GetContentTypeFromMimeType(const CString& mimeType)
{
  if (mimeType.Find(L"image/") >= 0)
  {
    return CFilter::contentTypeImage;
  }
  if (mimeType.Find(L"text/css") >= 0)
  {
    return CFilter::contentTypeStyleSheet;
  }
  if ((mimeType.Find(L"application/javascript") >= 0) || (mimeType.Find(L"application/json") >= 0))
  {
    return CFilter::contentTypeScript;
  }
  if (mimeType.Find(L"application/x-shockwave-flash") >= 0)
  {
    return CFilter::contentTypeObject;
  }
  if (mimeType.Find(L"text/html") >= 0)
  {
    return CFilter::contentTypeSubdocument;
  }
  // It is important to have this check last, since it is rather generic, and might overlay text/html, for example
  if (mimeType.Find(L"xml") >= 0)
  {
    return CFilter::contentTypeXmlHttpRequest;
  }

  return CFilter::contentTypeAny;
}

int WBPassthruSink::GetContentTypeFromURL(const std::wstring& src)
{
  CString srcLegacy = ToCString(src);
  CString srcExt = srcLegacy;

  int pos = 0;
  if ((pos = srcLegacy.Find('?')) > 0)
  {
    srcExt = srcLegacy.Left(pos);
  }

  int lastDotIndex = srcExt.ReverseFind('.');
  if (lastDotIndex < 0)
    return CFilter::contentTypeAny;
  CString ext = srcExt.Mid(lastDotIndex);
  if (ext == L".jpg" || ext == L".gif" || ext == L".png" || ext == L".jpeg")
  {
    return CFilter::contentTypeImage;
  }
  else if (ext == L".css")
  {
    return CFilter::contentTypeStyleSheet;
  }
  else if (ext.Right(3) == L".js")
  {
    return CFilter::contentTypeScript;
  }
  else if (ext == L".xml")
  {
    return CFilter::contentTypeXmlHttpRequest;
  }
  else if (ext == L".swf")
  {
    return CFilter::contentTypeObject;
  }
  else if (ext == L".jsp" || ext == L".php" || ext == L".html")
  {
    return CFilter::contentTypeSubdocument;
  }
  return CFilter::contentTypeAny;
}

int WBPassthruSink::GetContentType(const CString& mimeType, const std::wstring& domain, const std::wstring& src)
{
  // No referer or mime type
  // BINDSTRING_XDR_ORIGIN works only for IE v8+
  if (mimeType.IsEmpty() && domain.empty() && AdblockPlus::IE::InstalledMajorVersion() >= 8)
  {
    return CFilter::contentTypeXmlHttpRequest;
  }
  int contentType = GetContentTypeFromMimeType(mimeType);
  if (contentType == CFilter::contentTypeAny)
  {
    contentType = GetContentTypeFromURL(src);
  }
  return contentType;
}

////////////////////////////////////////////////////////////////////////////////////////
//WBPassthruSink
//Monitor and/or cancel every request and responde
//WB makes, including images, sounds, scripts, etc
////////////////////////////////////////////////////////////////////////////////////////
HRESULT WBPassthruSink::OnStart(LPCWSTR szUrl, IInternetProtocolSink *pOIProtSink,
                                IInternetBindInfo *pOIBindInfo, DWORD grfPI, HANDLE_PTR dwReserved,
                                IInternetProtocol* pTargetProtocol, bool& handled)
{
  m_pTargetProtocol = pTargetProtocol;
  return BaseClass::OnStart(szUrl, pOIProtSink, pOIBindInfo, grfPI, dwReserved, pTargetProtocol);
}

HRESULT WBPassthruSink::OnRead(void* pv, ULONG cb, ULONG* pcbRead)
{
  if (!pv || !pcbRead)
  {
    return E_POINTER;
  }
  *pcbRead = 0;

  if (PassthroughAPP::CustomSinkStartPolicy<WBPassthru, WBPassthruSink>::GetProtocol(this)->m_shouldSupplyCustomContent)
  {
    ULONG blockedByABPPageSize = static_cast<ULONG>(g_blockedByABPPage.size());
    auto positionGrow = std::min<ULONG>(cb, static_cast<ULONG>(blockedByABPPageSize - m_currentPositionOfSentPage));
    if (positionGrow == 0) {
      return S_FALSE;
    }
    std::copy(g_blockedByABPPage.begin(), g_blockedByABPPage.begin() + positionGrow,
      stdext::make_checked_array_iterator(static_cast<char*>(pv), cb));
    *pcbRead = positionGrow;
    m_currentPositionOfSentPage += positionGrow;

    if (m_spInternetProtocolSink)
    {
      m_spInternetProtocolSink->ReportData(BSCF_INTERMEDIATEDATANOTIFICATION,
        static_cast<ULONG>(m_currentPositionOfSentPage), blockedByABPPageSize);
    }
    if (blockedByABPPageSize == m_currentPositionOfSentPage && m_spInternetProtocolSink)
    {
      m_spInternetProtocolSink->ReportData(BSCF_DATAFULLYAVAILABLE, blockedByABPPageSize, blockedByABPPageSize);
      m_spInternetProtocolSink->ReportResult(S_OK, 0, nullptr);
    }
    return S_OK;
  }
  return m_pTargetProtocol->Read(pv, cb, pcbRead);
}
STDMETHODIMP WBPassthruSink::Switch(
  /* [in] */ PROTOCOLDATA *pProtocolData)
{
  ATLASSERT(m_spInternetProtocolSink != 0);

  /*
  From Igor Tandetnik "itandetnik@mvps.org"
  "
  Beware multithreading. URLMon has this nasty habit of spinning worker 
  threads, not even bothering to initialize COM on them, and calling APP 
  methods on those threads. If you try to raise COM events directly from 
  such a thread, bad things happen (random crashes, events being lost). 
  You are only guaranteed to be on the main STA thread in two cases. 
  First, in methods of interfaces that were obtained with 
  IServiceProvider, such as IHttpNegotiage::BeginningTransaction or 
  IAuthenticate::Authenticate. Second, you can call 
  IInternetProtocolSink::Switch with PD_FORCE_SWITCH flag in 
  PROTOCOLDATA::grfFlags, eventually URLMon will turn around and call 
  IInternetProtocol::Continue on the main thread. 

  Or, if you happen to have a window handy that was created on the main 
  thread, you can post yourself a message.
  "
  */
  return m_spInternetProtocolSink ? m_spInternetProtocolSink->Switch(pProtocolData) : E_UNEXPECTED;
}

// This is the heuristic which detects the requests issued by Flash.ocx.
// It turned out that the implementation from ''Flash.ocx'' (tested version is 15.0.0.152)
// returns quite minimal configuration in comparison with the implementation from Microsofts'
// libraries (see grfBINDF and bindInfo.dwOptions). The impl from MS often includes something
// else.
bool WBPassthruSink::IsFlashRequest(const wchar_t* const* additionalHeaders)
{
  if (additionalHeaders && *additionalHeaders)
  {
    auto flashVersionHeader = ExtractHttpHeader<std::wstring>(*additionalHeaders, L"x-flash-version:", L"\n");
    if (!TrimString(flashVersionHeader).empty())
    {
      return true;
    }
  }
  ATL::CComPtr<IBindStatusCallback> bscb;
  if (SUCCEEDED(QueryServiceFromClient(&bscb)) && !!bscb)
  {
    DWORD grfBINDF = 0;
    BINDINFO bindInfo = {};
    bindInfo.cbSize = sizeof(bindInfo);
    if (SUCCEEDED(bscb->GetBindInfo(&grfBINDF, &bindInfo)) &&
      (BINDF_ASYNCHRONOUS | BINDF_ASYNCSTORAGE| BINDF_PULLDATA) == grfBINDF &&
      (BINDINFO_OPTIONS_ENABLE_UTF8 | BINDINFO_OPTIONS_USE_IE_ENCODING) == bindInfo.dwOptions
      )
    {
      return true;
    }
  }
  return false;
}

STDMETHODIMP WBPassthruSink::BeginningTransaction(LPCWSTR szURL, LPCWSTR szHeaders, DWORD dwReserved, LPWSTR* pszAdditionalHeaders)
{
  if (!szURL)
  {
    return E_POINTER;
  }
  std::wstring src = szURL;
  DEBUG_GENERAL(ToCString(src));

  std::string acceptHeader = ExtractHttpAcceptHeader(m_spTargetProtocol);

  if (pszAdditionalHeaders)
  {
    *pszAdditionalHeaders = nullptr;
  }

  CComPtr<IHttpNegotiate> httpNegotiate;
  QueryServiceFromClient(&httpNegotiate);
  // This fills the pszAdditionalHeaders with more headers. One of which is the Referer header, which we need.
  // There doesn't seem to be any other way to get this header before the request has been made.
  HRESULT nativeHr = httpNegotiate ? httpNegotiate->BeginningTransaction(szURL, szHeaders, dwReserved, pszAdditionalHeaders) : S_OK;

  if (pszAdditionalHeaders && *pszAdditionalHeaders)
  {
    m_boundDomain = ExtractHttpHeader<std::wstring>(*pszAdditionalHeaders, L"Referer:", L"\n");
  }
  m_boundDomain = TrimString(m_boundDomain);
  m_contentType = GetContentType(ATL::CString(acceptHeader.c_str()), m_boundDomain, src);
  CPluginTab* tab = CPluginClass::GetTab(::GetCurrentThreadId());
  CPluginClient* client = CPluginClient::GetInstance();

  if (tab && client)
  {
    std::wstring documentUrl = tab->GetDocumentUrl();
    // Page is identical to document => don't block
    if (documentUrl == src)
    {
      return nativeHr;
    }
    else if (CPluginSettings::GetInstance()->IsPluginEnabled() && !client->IsWhitelistedUrl(documentUrl))
    {
      if (tab->IsFrameCached(src))
      {
        m_contentType = CFilter::contentTypeSubdocument;
      }
    }
  }

  if (IsFlashRequest(pszAdditionalHeaders))
  {
    m_contentType = CFilter::EContentType::contentTypeObjectSubrequest;
  }

  if (pszAdditionalHeaders && *pszAdditionalHeaders && IsXmlHttpRequest(*pszAdditionalHeaders))
  {
    m_contentType = CFilter::EContentType::contentTypeXmlHttpRequest;
  }

  if (client->ShouldBlock(szURL, m_contentType, m_boundDomain, /*debug flag but must be set*/true))
  {
    // NOTE: Feeding custom HTML to Flash, instead of original object subrequest
    // doesn't have much sense. It also can manifest in unwanted result	
    // like video being blocked (See https://issues.adblockplus.org/ticket/1669)	
    // So we report blocked object subrequests as failed, not just empty HTML.
    m_isCustomResponse = m_contentType != CFilter::contentTypeObjectSubrequest; 
    return E_ABORT;
  }
  return nativeHr;
}

STDMETHODIMP WBPassthruSink::OnResponse(DWORD dwResponseCode, LPCWSTR szResponseHeaders, LPCWSTR szRequestHeaders, LPWSTR *pszAdditionalRequestHeaders)
{
  if (pszAdditionalRequestHeaders)
  {
    *pszAdditionalRequestHeaders = 0;
  }

  CComPtr<IHttpNegotiate> spHttpNegotiate;
  QueryServiceFromClient(&spHttpNegotiate);

  return spHttpNegotiate ? spHttpNegotiate->OnResponse(dwResponseCode, szResponseHeaders, szRequestHeaders, pszAdditionalRequestHeaders) : S_OK;
}

STDMETHODIMP WBPassthruSink::ReportProgress(ULONG ulStatusCode, LPCWSTR szStatusText)
{
  return m_spInternetProtocolSink ? m_spInternetProtocolSink->ReportProgress(ulStatusCode, szStatusText) : S_OK;
}

STDMETHODIMP WBPassthruSink::ReportResult(/* [in] */ HRESULT hrResult, /* [in] */ DWORD dwError, /* [in] */ LPCWSTR szResult)
{
  if (m_isCustomResponse)
  {
    // Don't notify the client about aborting of the operation, thus don't call BaseClass::ReportResult.
    // Current method is called by the original protocol implementation and we are intercepting the
    // call here and eating it, we will call the proper ReportResult later by ourself.
    return S_OK;
  }
  return BaseClass::ReportResult(hrResult, dwError, szResult);
}


WBPassthru::WBPassthru()
  : m_shouldSupplyCustomContent(false)
{
}

STDMETHODIMP WBPassthru::Start(LPCWSTR szUrl, IInternetProtocolSink *pOIProtSink,
    IInternetBindInfo *pOIBindInfo, DWORD grfPI, HANDLE_PTR dwReserved)
{
  ATLASSERT(m_spInternetProtocol != 0);
  if (!m_spInternetProtocol)
  {
    return E_UNEXPECTED;
  }

  return OnStart(szUrl, pOIProtSink, pOIBindInfo, grfPI, dwReserved, m_spInternetProtocol);
}

STDMETHODIMP WBPassthru::Read(/* [in, out] */ void *pv,/* [in] */ ULONG cb,/* [out] */ ULONG *pcbRead)
{
  WBPassthruSink* pSink = GetSink();
  return pSink->OnRead(pv, cb, pcbRead);
}
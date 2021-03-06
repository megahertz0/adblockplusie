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

#pragma once
#include <cstdint>
#include "ProtocolCF.h"
#include "ProtocolImpl.h"
#define IE_MAX_URL_LENGTH 2048

class WBPassthruSink :
	public PassthroughAPP::CInternetProtocolSinkWithSP<WBPassthruSink, CComMultiThreadModel>,
	public IHttpNegotiate
{
	typedef PassthroughAPP::CInternetProtocolSinkWithSP<WBPassthruSink, CComMultiThreadModel> BaseClass;

public:
	WBPassthruSink();

	uint64_t m_currentPositionOfSentPage;
	CComPtr<IInternetProtocol> m_pTargetProtocol;
	int m_contentType;
	std::wstring m_boundDomain;
	bool m_isCustomResponse;

	int GetContentTypeFromMimeType(const CString& mimeType);
  int GetContentTypeFromURL(const std::wstring& src);
  int GetContentType(const CString& mimeType, const std::wstring& domain, const std::wstring& src);
	bool IsFlashRequest(const wchar_t* const* additionalHeaders);
public:
	BEGIN_COM_MAP(WBPassthruSink)
		COM_INTERFACE_ENTRY(IHttpNegotiate)
		COM_INTERFACE_ENTRY_CHAIN(BaseClass)
	END_COM_MAP()

	BEGIN_SERVICE_MAP(WBPassthruSink)
		SERVICE_ENTRY(IID_IHttpNegotiate)
	END_SERVICE_MAP()

	STDMETHODIMP BeginningTransaction(
		/* [in] */ LPCWSTR szURL,
		/* [in] */ LPCWSTR szHeaders,
		/* [in] */ DWORD dwReserved,
		/* [out] */ LPWSTR *pszAdditionalHeaders);

	STDMETHODIMP OnResponse(
		/* [in] */ DWORD dwResponseCode,
		/* [in] */ LPCWSTR szResponseHeaders,
		/* [in] */ LPCWSTR szRequestHeaders,
		/* [out] */ LPWSTR *pszAdditionalRequestHeaders);

	HRESULT OnStart(LPCWSTR szUrl, IInternetProtocolSink *pOIProtSink,
		IInternetBindInfo *pOIBindInfo, DWORD grfPI, HANDLE_PTR dwReserved,
		IInternetProtocol* pTargetProtocol, bool& handled);
	HRESULT OnRead(void *pv, ULONG cb, ULONG* pcbRead);

	STDMETHODIMP ReportProgress(
		/* [in] */ ULONG ulStatusCode,
		/* [in] */ LPCWSTR szStatusText);

	STDMETHODIMP ReportResult(
		/* [in] */ HRESULT hrResult,
		/* [in] */ DWORD dwError,
		/* [in] */ LPCWSTR szResult);

	STDMETHODIMP Switch(
		/* [in] */ PROTOCOLDATA *pProtocolData);
};

class WBPassthru;
typedef PassthroughAPP::CustomSinkStartPolicy<WBPassthru, WBPassthruSink> WBStartPolicy;

class WBPassthru : public PassthroughAPP::CInternetProtocol<WBStartPolicy>
{
  typedef PassthroughAPP::CInternetProtocol<WBStartPolicy> BaseClass;
public:
  WBPassthru();
  // IInternetProtocolRoot
  STDMETHODIMP Start(LPCWSTR szUrl, IInternetProtocolSink *pOIProtSink,
    IInternetBindInfo *pOIBindInfo, DWORD grfPI, HANDLE_PTR dwReserved) override;

  //IInternetProtocol
  STDMETHODIMP Read(/* [in, out] */ void *pv,/* [in] */ ULONG cb,/* [out] */ ULONG *pcbRead) override;

  bool m_shouldSupplyCustomContent;
};

#include "AdblockPlus/DefaultWebRequest.h"
#include "WinInetErrorCodes.h"
#include <algorithm>
#include <Windows.h>
#include <winhttp.h>
#include <Shlwapi.h>

#include "Utils.h"

class WinHttpHandle
{
public:
  HINTERNET handle;
  WinHttpHandle(HINTERNET handle = 0) : handle(handle) {}

  ~WinHttpHandle()
  {
    if (handle) WinHttpCloseHandle(handle);
    handle = 0;
  }
  // Overloaded HINTERNET cast
  operator HINTERNET() { return handle; }

};


long WindowsErrorToGeckoError(DWORD err)
{
  // TODO: Add some more error code translations for easier debugging
  switch (err)
  {
  case ERROR_INVALID_HANDLE:
    return AdblockPlus::DefaultWebRequest::NS_ERROR_NOT_INITIALIZED;
  case ERROR_INTERNET_UNRECOGNIZED_SCHEME:
    return AdblockPlus::DefaultWebRequest::NS_ERROR_UNKNOWN_PROTOCOL;
  case ERROR_INTERNET_CONNECTION_ABORTED:
    return AdblockPlus::DefaultWebRequest::NS_ERROR_CONNECTION_REFUSED;
  case ERROR_INTERNET_INVALID_URL:
    return AdblockPlus::DefaultWebRequest::NS_ERROR_MALFORMED_URI;
  case ERROR_INTERNET_CONNECTION_RESET:
    return AdblockPlus::DefaultWebRequest::NS_ERROR_NET_RESET;
  case ERROR_INTERNET_TIMEOUT:
    return AdblockPlus::DefaultWebRequest::NS_ERROR_NET_TIMEOUT;
  case ERROR_INTERNET_SERVER_UNREACHABLE:
    return AdblockPlus::DefaultWebRequest::NS_ERROR_UNKNOWN_HOST;
  case ERROR_INTERNET_PROXY_SERVER_UNREACHABLE:
    return AdblockPlus::DefaultWebRequest::NS_ERROR_UNKNOWN_PROXY_HOST;

  default:
    return AdblockPlus::DefaultWebRequest::NS_CUSTOM_ERROR_BASE + err;
  }
}

BOOL GetProxySettings(std::wstring& proxyName, std::wstring& proxyBypass)
{
  BOOL bResult = TRUE;

  // Get Proxy config info.
  WINHTTP_CURRENT_USER_IE_PROXY_CONFIG proxyConfig;

  ::ZeroMemory(&proxyConfig, sizeof(proxyConfig));

  if (WinHttpGetIEProxyConfigForCurrentUser(&proxyConfig))
  {
    if (proxyConfig.lpszProxy != 0)
    {
      proxyName.assign(proxyConfig.lpszProxy);
    }
    if (proxyConfig.lpszProxyBypass != 0)
    {
      proxyBypass.assign(proxyConfig.lpszProxyBypass);
    }
  }
  else
  {
      bResult = FALSE;
  }

  // The strings need to be freed.
  if (proxyConfig.lpszProxy != NULL)
  {
    ::GlobalFree(proxyConfig.lpszProxy);
  }
  if (proxyConfig.lpszAutoConfigUrl != NULL)
  {
    ::GlobalFree(proxyConfig.lpszAutoConfigUrl);
  }
  if (proxyConfig.lpszProxyBypass!= NULL)
  {
    ::GlobalFree(proxyConfig.lpszProxyBypass);
  }

  return bResult;
}

void ParseResponseHeaders(HINTERNET hRequest, AdblockPlus::ServerResponse* result)
{
  if (!result)
  {
    throw std::runtime_error("ParseResponseHeaders - second parameter is 0");
  }

  if (!hRequest)
  {
    throw std::runtime_error("ParseResponseHeaders - request is 0");
    return;
  }
  // Parse the response headers
  BOOL res = 0;
  DWORD bufLen = 0;
  res = WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_RAW_HEADERS, WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER, &bufLen, WINHTTP_NO_HEADER_INDEX);
  if (bufLen == 0)
  {
    // There are not headers
    return;
  }
  std::wstring responseHeaders;
  responseHeaders.resize(bufLen / sizeof(std::wstring::value_type));
  res = WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_RAW_HEADERS, WINHTTP_HEADER_NAME_BY_INDEX, &responseHeaders[0], &bufLen, WINHTTP_NO_HEADER_INDEX);
  if (res)
  {
    // Iterate through each header. Separator is '\0'
    int nextHeaderNameStart = 0;
    int headerNameEnd = 0;
    int headerValueStart = 0;
    int prevHeaderStart = 0;
    while ((nextHeaderNameStart = responseHeaders.find(L'\0', nextHeaderNameStart)) != std::wstring::npos)
    {
      headerNameEnd = responseHeaders.find(L":", prevHeaderStart);
      headerValueStart = headerNameEnd + strlen(":");
      if ((headerNameEnd > nextHeaderNameStart) || (headerNameEnd == std::wstring::npos))
      {
        nextHeaderNameStart++;
        prevHeaderStart = nextHeaderNameStart;
        continue;
      }
      std::wstring headerNameW = responseHeaders.substr(prevHeaderStart, headerNameEnd - prevHeaderStart);
      std::wstring headerValueW = responseHeaders.substr(headerValueStart, nextHeaderNameStart - headerValueStart);

      headerNameW = AdblockPlus::Utils::TrimString(headerNameW);
      headerValueW = AdblockPlus::Utils::TrimString(headerValueW);
      
      std::string headerName = AdblockPlus::Utils::ToUTF8String(headerNameW.c_str(), headerNameW.length());
      std::string headerValue = AdblockPlus::Utils::ToUTF8String(headerValueW.c_str(), headerValueW.length());

      std::transform(headerName.begin(), headerName.end(), headerName.begin(), ::tolower);
      std::transform(headerValue.begin(), headerValue.end(), headerValue.begin(), ::tolower);

      result->responseHeaders.push_back(
        std::pair<std::string, std::string>(headerName, headerValue));

      nextHeaderNameStart++;
      prevHeaderStart = nextHeaderNameStart;
    }
  }

  // Get the response status code
  std::wstring statusStr;
  DWORD statusLen = 0;
  res = WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE, WINHTTP_HEADER_NAME_BY_INDEX, &statusStr[0], &statusLen, WINHTTP_NO_HEADER_INDEX);
  if (statusLen == 0)
  {
    throw std::exception("Can't parse the status code");
  }
  statusStr.resize(statusLen / sizeof(std::wstring::value_type));
  res = WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE, WINHTTP_HEADER_NAME_BY_INDEX, &statusStr[0], &statusLen, WINHTTP_NO_HEADER_INDEX);
  result->responseStatus = wcstol(statusStr.c_str(), 0, 10);
  result->status = AdblockPlus::DefaultWebRequest::NS_OK;

}

AdblockPlus::ServerResponse AdblockPlus::DefaultWebRequest::GET(
  const std::string& url, const HeaderList& requestHeaders) const
{
  AdblockPlus::ServerResponse result;
  result.status = NS_ERROR_FAILURE;
  result.responseStatus = 0;

  HRESULT hr;
  BOOL res;

  std::wstring canonizedUrl = Utils::CanonizeUrl(Utils::ToUTF16String(url, url.length()));

  std::string headersString;
  for (int i = 0; i < requestHeaders.size(); i++)
  {
    headersString += requestHeaders[i].first + ": ";
    headersString += requestHeaders[i].second + ";";
  }
  std::wstring headers = Utils::ToUTF16String(headersString, headersString.length());

  LPSTR outBuffer;
  DWORD downloadSize, downloaded;
  WinHttpHandle hSession(0), hConnect(0), hRequest(0);

  // Use WinHttpOpen to obtain a session handle.
  std::wstring proxyName, proxyBypass;
  
  GetProxySettings(proxyName, proxyBypass);
  if (proxyName.empty())
  {
    hSession.handle = WinHttpOpen(L"Adblock Plus", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  }
  else
  {
    hSession.handle = WinHttpOpen(L"Adblock Plus", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, proxyName.c_str(), proxyBypass.c_str(), 0);
  }
  if (!hSession)
  {
    result.status = NS_CUSTOM_ERROR_BASE;
    return result;
  }
  URL_COMPONENTS urlComponents;

  // Initialize the URL_COMPONENTS structure.
  ZeroMemory(&urlComponents, sizeof(urlComponents));
  urlComponents.dwStructSize = sizeof(urlComponents);

  // Set required component lengths to non-zero so that they are cracked.
  urlComponents.dwSchemeLength    = (DWORD)-1;
  urlComponents.dwHostNameLength  = (DWORD)-1;
  urlComponents.dwUrlPathLength   = (DWORD)-1;
  urlComponents.dwExtraInfoLength = (DWORD)-1;
  res = WinHttpCrackUrl(canonizedUrl.c_str(), canonizedUrl.length(), 0, &urlComponents);
  if (!res)
  {
    result.status = WindowsErrorToGeckoError(GetLastError());
    return result;
  }
  std::wstring hostName(urlComponents.lpszHostName, urlComponents.dwHostNameLength);
  bool isSecure = urlComponents.nScheme == INTERNET_SCHEME_HTTPS;
  if (isSecure)
  {
    hConnect.handle = WinHttpConnect(hSession, hostName.c_str(), urlComponents.nPort, 0);
  }
  else
  {
    hConnect.handle = WinHttpConnect(hSession, hostName.c_str(), urlComponents.nPort, 0);
  }


  // Create an HTTP request handle.
  if (!hConnect)
  {
    result.status = WindowsErrorToGeckoError(GetLastError());
    return result;
  }
  DWORD flags = 0;
  if (isSecure)
  {
    flags = WINHTTP_FLAG_SECURE;
  }
  hRequest.handle = WinHttpOpenRequest(hConnect, L"GET", urlComponents.lpszUrlPath, 0, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);

  // Send a request.
  if (!hRequest)
  {
    result.status = WindowsErrorToGeckoError(GetLastError());
    return result;
  }
  // TODO: Make sure for HTTP 1.1 "Accept-Encoding: gzip, deflate" doesn't HAVE to be set here
  if (headers.length() > 0)
  {
    res = ::WinHttpSendRequest(hRequest, headers.c_str(), headers.length(), WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
  }
  else
  {
    res = ::WinHttpSendRequest(hRequest, 0, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
  }

  // End the request.
  if (!res)
  {
    result.status = WindowsErrorToGeckoError(GetLastError());;
    return result;
  }

  res = WinHttpReceiveResponse(hRequest, 0);
  if (!res)
  {
    result.status = NS_ERROR_NO_CONTENT;
    return result;
  }

  ParseResponseHeaders(hRequest, &result);

  // Download the actual response
  // Keep checking for data until there is nothing left.
  do 
  {
    // Check for available data.
    downloadSize = 0;
    if( !WinHttpQueryDataAvailable(hRequest, &downloadSize))
    {
      result.responseStatus = WindowsErrorToGeckoError(GetLastError());
      break;
    }
    // Allocate space for the buffer.
    outBuffer = new char[downloadSize+1];
    if (!outBuffer)
    {
      //Out of memory?
      downloadSize=0;
      res = FALSE;
      break;
    }
    else
    {
      // Read the data.
      ZeroMemory(outBuffer, downloadSize+1);

      if( WinHttpReadData(hRequest, (LPVOID)outBuffer, downloadSize, &downloaded))
      {
        result.responseText.append(outBuffer, downloaded);
      }
      // Free the memory allocated to the buffer.
      delete[] outBuffer;
    }
  } while (downloadSize > 0);
  return result;
}
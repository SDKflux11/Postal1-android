////////////////////////////////////////////////////////////////////////////////
//
// Copyright 2016 RWS Inc, All Rights Reserved
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of version 2 of the GNU General Public License as published by
// the Free Software Foundation
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
//
// ProtoBSDIP.cpp
// Project: Postal
// 
// History:
//		08/04/97 BRH	Started.
//
//		08/06/97 BRH	Debugged - restored to working order.
//					MJR
//
//		08/07/97 BRH	Fixed error message that referred to RSocket
//
//		08/08/97 MJR	Upgraded to latest revision of plugin stuff.
//
//		08/09/97 MJR	Added broadcast support.
//
//		08/14/97 MJR	Fixed GetAddress() to avoid database function when
//							working with a dotted address.
//
//		08/18/97 MJR	Fixed stupid error in GetAddress() that caused crashes.
//
//		08/20/97 MJR	Added support for setting whether socket blocks or not.
//
//		09/02/97 MJR	Got rid of TRACE() on "would block" situations.
//
//					MJR	Fixed serious bug that caused functions to return -1
//							as the number of actual bytes sent/received if an
//							error occurred -- they now properly return 0 instead.
//
//		09/09/97 MJR	Added RSocket::errNotSupported return value to Open().
//
//		11/18/97	JMI	Added message and Close() in destructor for case when
//							an opened socket is never closed.  Trying to find memory
//							leak but this was apparently not the problem.
//
//////////////////////////////////////////////////////////////////////////////

#include "RSPiX.h"
#include "socket.h"
#include <ctype.h>
#include <string.h>


//////////////////////////////////////////////////////////////////////////////
// Portability helpers for WinSock vs POSIX/Android BSD sockets
//////////////////////////////////////////////////////////////////////////////
#ifdef WIN32
	#define SOCK_SELECT_NFDS(s) 0
	#define SOCK_SEND_FLAGS 0
	static inline bool IsWouldBlockError(int err)
		{
		return (err == WSAEWOULDBLOCK);
		}
#else
	#define SOCK_SELECT_NFDS(s) ((int)((s) + 1))
	#ifdef MSG_NOSIGNAL
		#define SOCK_SEND_FLAGS MSG_NOSIGNAL
	#else
		#define SOCK_SEND_FLAGS 0
	#endif
	static inline bool IsWouldBlockError(int err)
		{
		return (err == EWOULDBLOCK) || (err == EAGAIN);
		}
#endif


//////////////////////////////////////////////////////////////////////////////
// Static data
//////////////////////////////////////////////////////////////////////////////

WSADATA							RProtocolBSDIP::ms_WSAData;
bool								RProtocolBSDIP::ms_bDidStartup;
bool								RProtocolBSDIP::ms_bWSAStartup;
#ifdef WIN32
bool								RProtocolBSDIP::ms_bWSASetBlockingHook;
RSocket::FuncNum				RProtocolBSDIP::ms_funcnum;
RSocket::BLOCK_CALLBACK		RProtocolBSDIP::ms_callback;
#endif

//////////////////////////////////////////////////////////////////////////////
// Functions
//////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////
// Constructor
//////////////////////////////////////////////////////////////////////////////
RProtocolBSDIP::RProtocolBSDIP(void)
	{
	Init();
	}


//////////////////////////////////////////////////////////////////////////////
// Destructor
//////////////////////////////////////////////////////////////////////////////
RProtocolBSDIP::~RProtocolBSDIP(void)
	{
	if (m_sock != INVALID_SOCKET)
		{
		TRACE("~RProtocolBSDIP(): Closing socket you forgot to, hoser!\n");

		Close();
		}
	}


//////////////////////////////////////////////////////////////////////////////
// Reset
//////////////////////////////////////////////////////////////////////////////
void RProtocolBSDIP::Reset(void)
	{
	Close();
	Init();
	RProtocol::Reset();
	}


//////////////////////////////////////////////////////////////////////////////
// Init
//////////////////////////////////////////////////////////////////////////////
void RProtocolBSDIP::Init(void)
	{
	m_sock = INVALID_SOCKET;
	m_sType = 0;
	m_bNonBlocking = false;
	RProtocol::Init();
	}


//////////////////////////////////////////////////////////////////////////////
//	Startup socket API.  This is normally called from the socket's Startup
// function.  Since we have multiple protocols using the same winsock
// code, the Startup and Shutdown will have to be called after creating
//	an RSocket and before calling Open()
//////////////////////////////////////////////////////////////////////////////
int16_t RProtocolBSDIP::Startup(void)
	{
	int16_t sResult = 0;
	
	// Only do this once, but multiple calls are not considered an error
	if (!ms_bDidStartup)
		{
		if (WSAStartup(MAKEWORD(1,1), &ms_WSAData) == 0)
			{
			ms_bWSAStartup = true;
			
			#ifdef WIN32
			if (LOBYTE(ms_WSAData.wVersion) == 1 && HIBYTE(ms_WSAData.wVersion) == 1)
				{
				ms_callback = 0;
				if (WSASetBlockingHook(RProtocolBSDIP::BlockingHook) != NULL)
					{
					ms_bWSASetBlockingHook = true;
					ms_bDidStartup = true;
					ms_prototype = RSocket::TCPIP;
					}
				else
					{
					sResult = -1;
					TRACE("RProtocolBSDIP::Hook(): Error returned by WSASetBlockingHook(): %ld\n", WSAGetLastError());
					}
				}
			else
				{
				sResult = -1;
				TRACE("RProtocolBSDIP::Startup(): Incorrect version of WinSock DLL!\n");
				}
			#else
			// POSIX / Android BSD sockets require no DLL initialization
			ms_bDidStartup = true;
			ms_prototype = RSocket::TCPIP;
			#endif
			}
		else
			{
			sResult = -1;
			TRACE("RProtocolBSDIP::Startup(): Incorrect version of WinSock DLL or can't find WinSock DLL!\n");
			}
		
		// If there's an error, clean up after ourselves
		if (sResult)
			Shutdown();
		}
	
	return sResult;
	}


//////////////////////////////////////////////////////////////////////////////
//	Shutdown socket API.
//////////////////////////////////////////////////////////////////////////////
void RProtocolBSDIP::Shutdown(void)
	{
	#ifdef WIN32
	if (ms_bWSASetBlockingHook)
		{
		if (WSAUnhookBlockingHook() == SOCKET_ERROR)
			TRACE("RProtocolBSDIP::Unhook(): Error returned by WSAUnhookBlockingHook(): %ld\n", WSAGetLastError());
		
		ms_bWSASetBlockingHook = false;
		}
	#endif

	if (ms_bWSAStartup)
		{
		if (WSACleanup() == SOCKET_ERROR)
			TRACE("RProtocolBSDIP::Shutdown(): Error returned by WSACleanup(): %ld\n", WSAGetLastError());
		
		ms_bWSAStartup = false;
		}
	
	// Set protocol
	ms_prototype = RSocket::NO_PROTOCOL;
	
	ms_bDidStartup = false;
	}


//////////////////////////////////////////////////////////////////////////////
// Open socket
// A return value of RSocket::errNotSupported means this protocol is
// not supported.
//////////////////////////////////////////////////////////////////////////////
int16_t RProtocolBSDIP::Open(							// Returns 0 if successful, non-zero otherwise
	uint16_t usPort,								// In:  Port number or 0 for any port
	int16_t sType,											// In:  Any one RSocket::typ* enum
	int16_t sOptionFlags,									// In:  Any combo of RSocket::opt* enums
	RSocket::BLOCK_CALLBACK callback /*NULL */)	// In:  Blocking callback (or NULL to keep current)
	{
	int16_t sResult = 0;
	
	if (ms_bDidStartup)
		{
		if (m_sock == INVALID_SOCKET)
			{
			if (callback != NULL)
				m_callback = callback;
			#ifdef WIN32
			ms_callback = m_callback;
			#endif

			// Save type
			m_sType = sType;
			m_bNonBlocking = false;
			
			// Select socket type
			int iType = 0;
			if (sType == RSocket::typStream)
				iType = SOCK_STREAM;
			else if (sType == RSocket::typDatagram)
				iType = SOCK_DGRAM;
			else
				{
				sResult = -1;
				TRACE("RProtocolBSDIP::Open(): Invalid type specified!\n");
				}

			if (sResult == 0)
				{
				#ifdef WIN32
				ms_funcnum = RSocket::OtherFunc;
				#endif

				m_sock = socket(AF_INET, iType, 0);
				if (m_sock != INVALID_SOCKET)
					{
					// Set defaults
					m_bListening = false;
					m_bConnected = false;
					m_bConnecting = false;

					#ifndef WIN32
					// Allow quick port reuse when re-hosting / re-browsing on Android/Linux
					if (usPort != 0)
						{
						int reuse = 1;
						setsockopt(m_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
						}
					#endif
					
					// Check if they want blocking turned off
					if (sOptionFlags & RSocket::optDontBlock)
						{
						#ifdef WIN32
						u_long ulEnableNonBlockingMode = 1;
						if (ioctlsocket(m_sock, FIONBIO, &ulEnableNonBlockingMode) == SOCKET_ERROR)
							{
							sResult = -1;
							TRACE("RProtocolBSDIP::Open(): Error setting non-blocking mode: %ld\n", WSAGetLastError());
							}
						#else
						int flags = fcntl(m_sock, F_GETFL, 0);
						if ((flags < 0) || (fcntl(m_sock, F_SETFL, flags | O_NONBLOCK) < 0))
							{
							sResult = -1;
							TRACE("RProtocolBSDIP::Open(): Error setting O_NONBLOCK: %d\n", WSAGetLastError());
							}
						#endif
						if (sResult == 0)
							m_bNonBlocking = true;
						}

					// Check if they want coalescing turned off
					if ((sOptionFlags & RSocket::optDontCoalesce) && !sResult)
						{
						if (sType == RSocket::typStream)
							{
							int optval = 1;
							if (setsockopt(m_sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&optval, sizeof(optval)) == SOCKET_ERROR)
								{
								sResult = -1;
								TRACE("RProtocolBSDIP::Open(): Error setting TCP_NODELAY: %d\n", (int)WSAGetLastError());
								}
							}
						else
							{
							sResult = -1;
							TRACE("RProtocolBSDIP::Open(): optDontCoalesce is only valid for typStream!\n");
							}
						}

					// Check if they want lingering turned off
					if ((sOptionFlags & RSocket::optDontWaitOnClose) && !sResult)
						{
						if (sType == RSocket::typStream)
							{
							#ifdef WIN32
							int optval = 1;
							if (setsockopt(m_sock, SOL_SOCKET, SO_DONTLINGER, (char*)&optval, sizeof(optval)) == SOCKET_ERROR)
								{
								sResult = -1;
								TRACE("RProtocolBSDIP::Open(): Error setting SO_DONTLINGER: %ld\n", WSAGetLastError());
								}
							#else
							LINGER lingerOpt;
							lingerOpt.l_onoff = 0;
							lingerOpt.l_linger = 0;
							if (setsockopt(m_sock, SOL_SOCKET, SO_LINGER, (const char*)&lingerOpt, sizeof(lingerOpt)) == SOCKET_ERROR)
								{
								sResult = -1;
								TRACE("RProtocolBSDIP::Open(): Error setting SO_LINGER: %d\n", WSAGetLastError());
								}
							#endif
							}
						else
							{
							sResult = -1;
							TRACE("RProtocolBSDIP::Open(): optDontWaitOnClose is only valid for typStream!\n");
							}
						}
					
					if (sResult == 0)
						{
						// Setup local address (any address is fine)
						SOCKADDR_IN addr;
						memset(&addr, 0, sizeof(addr));
						addr.sin_family = AF_INET;
						addr.sin_port = htons(usPort);
						addr.sin_addr.s_addr = htonl(INADDR_ANY);

						// Bind socket to local address
						if (::bind(m_sock, (SOCKADDR*) &addr, sizeof(addr)) != SOCKET_ERROR)
							{
							// success
							}
						else
							{
							sResult = -1;
							TRACE("RProtocolBSDIP::Open(): Error returned by bind(): %d\n", (int)WSAGetLastError());
							}
						}

					// If anything went wrong, close the socket
					if (sResult)
						Close();
					}
				else
					{
					int iErr = WSAGetLastError();
					#ifdef WIN32
					if (iErr == WSAEAFNOSUPPORT)
						sResult = RSocket::errNotSupported;
					#else
					if ((iErr == EAFNOSUPPORT) || (iErr == EPROTONOSUPPORT))
						sResult = RSocket::errNotSupported;
					#endif
					else
						sResult = -1;
					TRACE("RProtocolBSDIP::Open(): Error returned by socket(): %d\n", iErr);
					}
				}
			}
		else
			{
			sResult = -1;
			TRACE("RProtocolBSDIP::Open(): Socket already open!\n");
			}
		}
	else
		{
		sResult = -1;
		TRACE("RProtocolBSDIP::Open(): Didn't call Startup()!\n");
		}

	return sResult;
	}


//////////////////////////////////////////////////////////////////////////////
// Close socket
//////////////////////////////////////////////////////////////////////////////
int16_t RProtocolBSDIP::Close(							// Returns 0 if successfull, non-zero otherwise
	bool bForceNow /*= true */)						// In:  'true' means do it now, false follows normal rules
	{
	int16_t sResult = 0;

	// Only close it if it's open, but don't consider it an error if it isn't
	if (m_sock != INVALID_SOCKET)
		{
		#ifdef WIN32
		ms_callback = m_callback;
		#endif
		
		// See if they want to force this to happen RIGHT NOW
		if (bForceNow && (m_sType == RSocket::typStream))
			{
			#ifdef WIN32
			int optval = 1;
			ms_funcnum = RSocket::OtherFunc;
			if (setsockopt(m_sock, SOL_SOCKET, SO_DONTLINGER, (char*)&optval, sizeof(optval)) == SOCKET_ERROR)
				{
				sResult = -1;
				TRACE("RProtocolBSDIP::Close(): Error setting SO_DONTLINGER: %ld\n", WSAGetLastError());
				}
			#else
			LINGER lingerOpt;
			lingerOpt.l_onoff = 0;
			lingerOpt.l_linger = 0;
			setsockopt(m_sock, SOL_SOCKET, SO_LINGER, (const char*)&lingerOpt, sizeof(lingerOpt));
			#endif
			}

		if (sResult == 0)
			{
			#ifdef WIN32
			ms_funcnum = RSocket::OtherFunc;
			#endif
			if (closesocket(m_sock) != SOCKET_ERROR)
				{
				// Reset flags
				m_bListening = false;
				m_bConnected = false;
				m_bConnecting = false;
				m_bNonBlocking = false;
				
				// Mark it as invalid
				m_sock = INVALID_SOCKET;
				}
			else
				{
				int iErr = WSAGetLastError();
				if (IsWouldBlockError(iErr))
					sResult = RSocket::errWouldBlock;
				else
					{
					sResult = -1;
					TRACE("RProtocolBSDIP::Close(): Error returned by closesocket(): %d\n", iErr);
					}
				}
			}
		}
	
	return sResult;
	}


////////////////////////////////////////////////////////////////////////////////
// Set socket to broadcast mode
////////////////////////////////////////////////////////////////////////////////
int16_t RProtocolBSDIP::Broadcast(void)				// Returns 0 if successfull, non-zero otherwise
	{
	int16_t sResult = 0;
	
	if (m_sock != INVALID_SOCKET)
		{
		if (!m_bListening)
			{
			#ifdef WIN32
			ms_callback = m_callback;
			ms_funcnum = RSocket::OtherFunc;
			#endif
			
			int optval = 1;
			if (setsockopt(m_sock, SOL_SOCKET, SO_BROADCAST, (const char*)&optval, sizeof(optval)) == SOCKET_ERROR)
				{
				sResult = -1;
				TRACE("RProtocolBSDIP::Broadcast(): Error setting SO_BROADCAST: %d\n", (int)WSAGetLastError());
				}
			}
		else
			{
			sResult = -1;
			TRACE("RProtocolBSDIP::Broadcast(): Socket is already listening!\n");
			}
		}
	else
		{
		sResult = -1;
		TRACE("RProtocolBSDIP::Broadcast(): Socket is not open!\n");
		}
	
	return sResult;
	}


//////////////////////////////////////////////////////////////////////////////
// Set socket to listen for connection requests
//////////////////////////////////////////////////////////////////////////////
int16_t RProtocolBSDIP::Listen(							// Returns 0 if successfull, non-zero otherwise
	int16_t sMaxQueued /* = 5*/)							// In:  Maximum number of queued connection requests
	{
	int16_t sResult = 0;
	
	if (m_sock != INVALID_SOCKET)
		{
		if (!m_bListening)
			{
			if ((sMaxQueued >= 1) && (sMaxQueued <= 5))
				{
				#ifdef WIN32
				ms_callback = m_callback;
				ms_funcnum = RSocket::OtherFunc;
				#endif
				
				if (listen(m_sock, (int)sMaxQueued) != SOCKET_ERROR)
					{
					// Socket is listening
					m_bListening = true;
					}
				else
					{
					sResult = -1;
					TRACE("RProtocolBSDIP::Listen(): Error returned by listen(): %d\n", (int)WSAGetLastError());
					}
				}
			else
				{
				sResult = -1;
				TRACE("RProtocolBSDIP::Listen(): Max queued connections must be between 1 and 5!\n");
				}
			}
		else
			{
			sResult = -1;
			TRACE("RProtocolBSDIP::Listen(): Socket is already listening!\n");
			}
		}
	else
		{
		sResult = -1;
		TRACE("RProtocolBSDIP::Listen(): Socket is not open!\n");
		}
	
	return sResult;
	}


//////////////////////////////////////////////////////////////////////////////
// Accept request for connection
//////////////////////////////////////////////////////////////////////////////
int16_t RProtocolBSDIP::Accept(						// Returns 0 on success, non-zero otherwise
	RSocket::RProtocol* pProtocolClient,			// I/O: Client protocol
	RSocket::Address* paddressClient)				// Out: Client address
	{
	int16_t sResult = 0;
	
	if (m_sock != INVALID_SOCKET)
		{
		if (m_bListening)
			{
			#ifdef WIN32
			ms_callback = m_callback;
			#endif
			
			RProtocolBSDIP* pClient = (RProtocolBSDIP*)pProtocolClient;
			if (pClient->m_sock == INVALID_SOCKET)
				{
				AddressIP addressClient;
				memset(&addressClient, 0, sizeof(addressClient));
				addressClient.prototype = RSocket::TCPIP;
				addressClient.lAddressLen = sizeof(addressClient.address);
				
				#ifdef WIN32
				ms_funcnum = RSocket::AcceptFunc;
				pClient->m_sock = accept(m_sock, (SOCKADDR*)&(addressClient.address), (int*)(&addressClient.lAddressLen));
				#else
				SOCKLEN sockLen = (SOCKLEN)sizeof(addressClient.address);
				pClient->m_sock = accept(m_sock, (SOCKADDR*)&(addressClient.address), &sockLen);
				addressClient.lAddressLen = (int32_t)sockLen;
				#endif

				if (pClient->m_sock != INVALID_SOCKET)
					{
					pClient->m_sType = RSocket::typStream;
					pClient->m_bListening = false;
					pClient->m_bConnected = true;
					pClient->m_bConnecting = false;

					#ifndef WIN32
					// On Linux/Android, accept() does NOT inherit O_NONBLOCK from the listening socket!
					if (m_bNonBlocking)
						{
						int flags = fcntl(pClient->m_sock, F_GETFL, 0);
						if (flags >= 0)
							fcntl(pClient->m_sock, F_SETFL, flags | O_NONBLOCK);
						pClient->m_bNonBlocking = true;
						}
					int noDelay = 1;
					setsockopt(pClient->m_sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&noDelay, sizeof(noDelay));
					#endif

					// If caller requested client address, return it now
					if (paddressClient != NULL)
						{
						memcpy(paddressClient, &addressClient, sizeof(RSocket::Address));
						}
					}
				else
					{
					int iErr = WSAGetLastError();
					if (IsWouldBlockError(iErr))
						sResult = RSocket::errWouldBlock;
					else
						{
						sResult = -1;
						TRACE("RProtocolBSDIP::AcceptClient(): Error returned by accept(): %d\n", iErr);
						}
					}
				}
			else
				{
				sResult = -1;
				TRACE("RProtocolBSDIP::AcceptClient(): Specified client socket is already in use!\n");
				}
			}
		else
			{
			sResult = -1;
			TRACE("RProtocolBSDIP::AcceptClient(): Socket is not listening!\n");
			}
		}
	else
		{
		sResult = -1;
		TRACE("RProtocolBSDIP::AcceptClient(): Socket is not open!\n");
		}
	
	return sResult;
	}


//////////////////////////////////////////////////////////////////////////////
// Connect to address
//////////////////////////////////////////////////////////////////////////////
int16_t RProtocolBSDIP::Connect(						// Returns 0 if successfull, non-zero otherwise
	RSocket::Address* paddress)						// In:  Remote address to connect to
	{
	int16_t sResult = 0;

	if (m_sock != INVALID_SOCKET)
		{
		if (!m_bListening)
			{
			#ifdef WIN32
			ms_callback = m_callback;
			#endif
				
			if (!m_bConnecting)
				{
				#ifdef WIN32
				ms_funcnum = RSocket::OtherFunc;
				#endif
				if (connect(m_sock, (SOCKADDR*)&(paddress->address), sizeof(SOCKADDR_IN)) != SOCKET_ERROR)
					{
					m_bConnecting = false;
					m_bConnected = true;
					}
				else
					{
					int iErr = WSAGetLastError();
					#ifdef WIN32
					if (iErr == WSAEWOULDBLOCK)
						{
						sResult = RSocket::errWouldBlock;
						m_bConnected = false;
						m_bConnecting = true;
						}
					#else
					if ((iErr == EINPROGRESS) || (iErr == EALREADY) || IsWouldBlockError(iErr))
						{
						sResult = RSocket::errWouldBlock;
						m_bConnected = false;
						m_bConnecting = true;
						}
					else if (iErr == EISCONN)
						{
						m_bConnecting = false;
						m_bConnected = true;
						}
					#endif
					else
						{
						sResult = -1;
						TRACE("RProtocolBSDIP::Connect(): Error returned by connect(): %d\n", iErr);
						}
					}
				}
			else
				{
				// The socket is trying to connect, so poll completion status
				fd_set wset;
				fd_set eset;
				FD_ZERO(&wset);
				FD_ZERO(&eset);
				FD_SET(m_sock, &wset);
				FD_SET(m_sock, &eset);

				TIMEVAL timeval = { 0, 0 };

				#ifdef WIN32
				ms_funcnum = RSocket::SelectFunc;
				#endif
				int32_t lCount = select(SOCK_SELECT_NFDS(m_sock), NULL, &wset, &eset, &timeval);
				if (lCount > 0)
					{
					#ifdef WIN32
					if (FD_ISSET(m_sock, &eset))
						{
						m_bConnecting = false;
						m_bConnected = false;
						sResult = -1;
						TRACE("RProtocolBSDIP::Connect(): Non-blocking connection attempt has failed!\n");
						}
					else if (FD_ISSET(m_sock, &wset))
						{
						m_bConnecting = false;
						m_bConnected = true;
						}
					else
						{
						sResult = RSocket::errWouldBlock;
						}
					#else
					int soErr = 0;
					SOCKLEN soLen = sizeof(soErr);
					if ((getsockopt(m_sock, SOL_SOCKET, SO_ERROR, (char*)&soErr, &soLen) == 0) && (soErr == 0))
						{
						m_bConnecting = false;
						m_bConnected = true;
						}
					else
						{
						m_bConnecting = false;
						m_bConnected = false;
						sResult = -1;
						TRACE("RProtocolBSDIP::Connect(): Non-blocking connection failed (SO_ERROR=%d)!\n", soErr);
						}
					#endif
					}
				else if (lCount == 0)
					{
					sResult = RSocket::errWouldBlock;
					}
				else
					{
					m_bConnecting = false;
					m_bConnected = false;
					sResult = -1;
					TRACE("RProtocolBSDIP::Connect(): select() error while connecting!\n");
					}
				}
			}
		else
			{
			sResult = -1;
			TRACE("RProtocolBSDIP::Connect(): Socket is listening -- can't connect!\n");
			}
		}
	else
		{
		sResult = -1;
		TRACE("RProtocolBSDIP::Connect(): Socket is not open!\n");
		}
	
	return sResult;
	}


//////////////////////////////////////////////////////////////////////////////
// Send data to connected socket
// NOTE: If an error occurs, plActualBytes will return 0.
//////////////////////////////////////////////////////////////////////////////
int16_t RProtocolBSDIP::Send(							// Returns 0 on success, non-zero otherwise
	void* pBuf,												// In:  Pointer to data buffer
	int32_t lNumBytes,										// In:  Number of bytes to send
	int32_t* plActualBytes)									// Out: Actual number of bytes sent
	{
	int16_t sResult = 0;
	*plActualBytes = 0;
	
	if (m_sock != INVALID_SOCKET)
		{
		if (!m_bListening)
			{
			if (m_bConnected)
				{
				#ifdef WIN32
				ms_callback = m_callback;
				ms_funcnum = RSocket::OtherFunc;
				#endif
				
				ssize_t nSent = send(m_sock, (const char*)pBuf, (size_t)lNumBytes, SOCK_SEND_FLAGS);
				if (nSent >= 0)
					{
					*plActualBytes = (int32_t)nSent;
					}
				else
					{
					*plActualBytes = 0;
					int iErr = WSAGetLastError();
					if (IsWouldBlockError(iErr))
						sResult = RSocket::errWouldBlock;
					else
						{
						sResult = -1;
						TRACE("RProtocolBSDIP::Send(): Error returned by send(): %d\n", iErr);
						}
					}
				}
			else
				{
				sResult = -1;
				TRACE("RProtocolBSDIP::Send(): Socket not connected!\n");
				}
			}
		else
			{
			sResult = -1;
			TRACE("RProtocolBSDIP::Send(): Socket is listening -- can't send data!\n");
			}
		}
	else
		{
		sResult = -1;
		TRACE("RProtocolBSDIP::Send(): Socket is not open!\n");
		}
	
	return sResult;
	}


//////////////////////////////////////////////////////////////////////////////
// Send data to specified address
// NOTE: If an error occurs, plActualBytes will return 0.
//////////////////////////////////////////////////////////////////////////////
int16_t RProtocolBSDIP::SendTo(							// Returns 0 on success, non-zero otherwise
	void* pBuf,												// In:  Pointer to data buffer
	int32_t lNumBytes,										// In:  Number of bytes to send
	int32_t* plActualBytes,									// Out: Actual number of bytes sent
	RSocket::Address* paddress)						// In:  Address to send to
	{
	int16_t sResult = 0;
	*plActualBytes = 0;
	
	if (m_sock != INVALID_SOCKET)
		{
		if (!m_bListening)
			{
			#ifdef WIN32
			ms_callback = m_callback;
			ms_funcnum = RSocket::OtherFunc;
			#endif

			AddressIP* pip = (AddressIP*)paddress;
			bool bSentAnyBroadcast = false;

			#ifndef WIN32
			// On Android Wi-Fi / Mobile Hotspots without cellular data, 255.255.255.255 may fail with ENETUNREACH
			// because there is no default route. Also send directed subnet broadcasts (e.g. 192.168.43.255) on all
			// active IPv4 broadcast interfaces so LAN / Hotspot Co-Op server discovery works offline!
			if (pip->address.sin_addr.s_addr == htonl(INADDR_BROADCAST))
				{
				char ifBuf[4096];
				struct ifconf ifc;
				memset(&ifc, 0, sizeof(ifc));
				ifc.ifc_len = sizeof(ifBuf);
				ifc.ifc_buf = ifBuf;
				if (ioctl(m_sock, SIOCGIFCONF, &ifc) == 0)
					{
					int numReqs = ifc.ifc_len / (int)sizeof(struct ifreq);
					struct ifreq* ifr = ifc.ifc_req;
					for (int i = 0; i < numReqs; i++)
						{
						struct ifreq reqFlags = ifr[i];
						if (ioctl(m_sock, SIOCGIFFLAGS, &reqFlags) == 0)
							{
							if ((reqFlags.ifr_flags & IFF_UP) &&
								(reqFlags.ifr_flags & IFF_BROADCAST) &&
								!(reqFlags.ifr_flags & IFF_LOOPBACK))
								{
								struct ifreq reqBrd = ifr[i];
								if (ioctl(m_sock, SIOCGIFBRDADDR, &reqBrd) == 0 &&
									reqBrd.ifr_broadaddr.sa_family == AF_INET)
									{
									SOCKADDR_IN brdAddr = pip->address;
									brdAddr.sin_addr = ((SOCKADDR_IN*)&reqBrd.ifr_broadaddr)->sin_addr;
									if (brdAddr.sin_addr.s_addr != htonl(INADDR_ANY) &&
										brdAddr.sin_addr.s_addr != htonl(INADDR_BROADCAST))
										{
										ssize_t nBrd = sendto(m_sock, (const char*)pBuf, (size_t)lNumBytes,
											SOCK_SEND_FLAGS, (SOCKADDR*)&brdAddr, sizeof(brdAddr));
										if (nBrd == lNumBytes)
											bSentAnyBroadcast = true;
										}
									}
								}
							}
						}
					}
				}
			#endif
			
			// Send data to the requested destination address
			SOCKLEN sockLen = (paddress->prototype == RSocket::TCPIP)
				? (SOCKLEN)sizeof(SOCKADDR_IN)
				: (SOCKLEN)paddress->lAddressLen;
			ssize_t nSent = sendto(m_sock, (const char*)pBuf, (size_t)lNumBytes, SOCK_SEND_FLAGS,
				(SOCKADDR*)&(paddress->address), sockLen);
			if (nSent >= 0)
				{
				*plActualBytes = (int32_t)nSent;
				}
			else if (bSentAnyBroadcast)
				{
				// Directed subnet broadcast succeeded even though 255.255.255.255 had no default route
				*plActualBytes = lNumBytes;
				}
			else
				{
				*plActualBytes = 0;
				int iErr = WSAGetLastError();
				if (IsWouldBlockError(iErr))
					sResult = RSocket::errWouldBlock;
				else
					{
					sResult = -1;
					TRACE("RProtocolBSDIP::SendTo(): Error returned by sendto(): %d\n", iErr);
					}
				}
			}
		else
			{
			sResult = -1;
			TRACE("RProtocolBSDIP::SendTo(): Socket is listening -- can't send data!\n");
			}
		}
	else
		{
		sResult = -1;
		TRACE("RProtocolBSDIP::SendTo(): Socket is not open!\n");
		}
	
	return sResult;
	}


//////////////////////////////////////////////////////////////////////////////
// Receive data - only valid with connected sockets
// NOTE: If an error occurs, plActualBytes will return 0.
//////////////////////////////////////////////////////////////////////////////
int16_t RProtocolBSDIP::Receive(						// Returns 0 on success, non-zero otherwise
	void* pBuf,												// In:  Pointer to data buffer
	int32_t lMaxBytes,										// In:  Maximum number of bytes that fit in the buffer
	int32_t* plActualBytes)									// Out: Actual number of bytes recieved into the buffer
	{
	int16_t sResult = 0;
	*plActualBytes = 0;
	
	if (m_sock != INVALID_SOCKET)
		{
		if (!m_bListening)
			{
			if (m_bConnected)
				{
				#ifdef WIN32
				ms_callback = m_callback;
				ms_funcnum = RSocket::OtherFunc;
				#endif
				
				ssize_t nRecv = recv(m_sock, (char*)pBuf, (size_t)lMaxBytes, 0);
				if (nRecv >= 0)
					{
					*plActualBytes = (int32_t)nRecv;
					}
				else
					{
					*plActualBytes = 0;
					int iErr = WSAGetLastError();
					if (IsWouldBlockError(iErr))
						sResult = RSocket::errWouldBlock;
					else
						{
						sResult = -1;
						TRACE("RProtocolBSDIP::Receive(): Error returned by recv(): %d\n", iErr);
						}
					}
				}
			else
				{
				sResult = -1;
				TRACE("RProtocolBSDIP::Receive(): Socket not connected!\n");
				}
			}
		else
			{
			sResult = -1;
			TRACE("RProtocolBSDIP::Receive(): Socket is listening -- can't receive data!\n");
			}
		}
	else
		{
		sResult = -1;
		TRACE("RProtocolBSDIP::Receive(): Socket is not open!\n");
		}
	
	return sResult;
	}


//////////////////////////////////////////////////////////////////////////////
// Receive data from the given address
// NOTE: If an error occurs, plActualBytes will return 0.
//////////////////////////////////////////////////////////////////////////////
int16_t RProtocolBSDIP::ReceiveFrom(					// Returns 0 on success, non-zero otherwise
	void* pBuf,												// In:  Pointer to data buffer
	int32_t lMaxBytes,										// In:  Maximum bytes that can fit in the buffer
	int32_t* plActualBytes,									// Out: Actual number of bytes recieved into buffer
	RSocket::Address* paddress)						// Out: Source address returned here
	{
	int16_t sResult = 0;
	*plActualBytes = 0;
	
	if (m_sock != INVALID_SOCKET)
		{
		if (!m_bListening)
			{
			#ifdef WIN32
			ms_callback = m_callback;
			ms_funcnum = RSocket::OtherFunc;
			#endif
			
			// Source address
			AddressIP addr;
			memset(&addr, 0, sizeof(addr));
			addr.prototype = RSocket::TCPIP;
			addr.lAddressLen = sizeof(addr.address);
			
			#ifdef WIN32
			ssize_t nRecv = recvfrom(m_sock, (char*)pBuf, lMaxBytes, 0, (SOCKADDR*)&(addr.address), (int*)(&addr.lAddressLen));
			#else
			SOCKLEN sockLen = (SOCKLEN)sizeof(addr.address);
			ssize_t nRecv = recvfrom(m_sock, (char*)pBuf, (size_t)lMaxBytes, 0, (SOCKADDR*)&(addr.address), &sockLen);
			addr.lAddressLen = (int32_t)sockLen;
			#endif

			if (nRecv >= 0)
				{
				*plActualBytes = (int32_t)nRecv;
				if (paddress != NULL)
					{
					memcpy(paddress, &addr, sizeof(RSocket::Address));
					}
				}
			else
				{
				*plActualBytes = 0;
				int iErr = WSAGetLastError();
				if (IsWouldBlockError(iErr))
					sResult = RSocket::errWouldBlock;
				else
					{
					sResult = -1;
					TRACE("RProtocolBSDIP::ReceiveFrom(): Error returned by recvfrom(): %d\n", iErr);
					}
				}
			}
		else
			{
			sResult = -1;
			TRACE("RProtocolBSDIP::ReceiveFrom(): Socket is listening -- can't receive data!\n");
			}
		}
	else
		{
		sResult = -1;
		TRACE("RProtocolBSDIP::ReceiveFrom(): Socket is not open!\n");
		}
	
	return sResult;
	}


//////////////////////////////////////////////////////////////////////////////
// Check if connection can be accepted without blocking
//////////////////////////////////////////////////////////////////////////////
bool RProtocolBSDIP::CanAcceptWithoutBlocking(void)
	{
	bool bResult = false;
	
	if (m_sock != INVALID_SOCKET)
		{
		if (m_bListening)
			{
			#ifdef WIN32
			ms_callback = m_callback;
			ms_funcnum = RSocket::SelectFunc;
			#endif
			
			fd_set set;
			FD_ZERO(&set);
			FD_SET(m_sock, &set);
			
			TIMEVAL timeval = { 0, 0 };
			
			int32_t lCount = select(SOCK_SELECT_NFDS(m_sock), &set, NULL, NULL, &timeval);
			if (lCount > 0)
				bResult = true;
			}
		else
			{
			TRACE("RProtocolBSDIP::CanAcceptWithoutBlocking(): Socket is not listening!\n");
			}
		}
	else
		{
		TRACE("RProtocolBSDIP::CanAcceptWithoutBlocking(): Socket is not open!\n");
		}
	
	return bResult;
	}


//////////////////////////////////////////////////////////////////////////////
// Check if socket can send without blocking.
//////////////////////////////////////////////////////////////////////////////
bool RProtocolBSDIP::CanSendWithoutBlocking(void)
	{
	bool bResult = false;
	
	if (m_sock != INVALID_SOCKET)
		{
		if (!m_bListening)
			{
			#ifdef WIN32
			ms_callback = m_callback;
			ms_funcnum = RSocket::SelectFunc;
			#endif
			
			fd_set set;
			FD_ZERO(&set);
			FD_SET(m_sock, &set);
			
			TIMEVAL timeval = { 0, 0 };
			
			int32_t lCount = select(SOCK_SELECT_NFDS(m_sock), NULL, &set, NULL, &timeval);
			if (lCount > 0)
				bResult = true;
			}
		else
			{
			TRACE("RProtocolBSDIP::CanSendWithoutBlocking(): Socket is listening -- can't send data!\n");
			}
		}
	else
		{
		TRACE("RProtocolBSDIP::CanSendWithoutBlocking(): Socket is not open!\n");
		}
	
	return bResult;
	}


//////////////////////////////////////////////////////////////////////////////
// Check if socket can be read without blocking.
//////////////////////////////////////////////////////////////////////////////
bool RProtocolBSDIP::CanReceiveWithoutBlocking(void)
	{
	bool bResult = false;
	
	if (m_sock != INVALID_SOCKET)
		{
		if (!m_bListening)
			{
			#ifdef WIN32
			ms_callback = m_callback;
			ms_funcnum = RSocket::SelectFunc;
			#endif
			
			fd_set set;
			FD_ZERO(&set);
			FD_SET(m_sock, &set);
			
			TIMEVAL timeval = { 0, 0 };
			
			int32_t lCount = select(SOCK_SELECT_NFDS(m_sock), &set, NULL, NULL, &timeval);
			if (lCount > 0)
				bResult = true;
			}
		else
			{
			TRACE("RProtocolBSDIP::CanReceiveWithoutBlocking(): Socket is listening -- can't receive data!\n");
			}
		}
	else
		{
		TRACE("RProtocolBSDIP::CanReceiveWithoutBlocking(): Socket is not open!\n");
		}
	
	return bResult;
	}


//////////////////////////////////////////////////////////////////////////////
// Check the number of bytes of data receivable without blocking.
//////////////////////////////////////////////////////////////////////////////
int32_t RProtocolBSDIP::CheckReceivableBytes(void)
	{
	int32_t lResult = 0;
	
	if (m_sock != INVALID_SOCKET)
		{
		if (!m_bListening)
			{
			#ifdef WIN32
			ms_callback = m_callback;
			ms_funcnum = RSocket::OtherFunc;
			u_long ulAvail = 0;
			if (ioctlsocket(m_sock, FIONREAD, &ulAvail) != SOCKET_ERROR)
				lResult = (int32_t)ulAvail;
			else
				TRACE("RProtocolBSDIP::CheckReceivableBytes(): Error returned by ioctlsocket(): %ld\n", WSAGetLastError());
			#else
			// On 64-bit LP64 Linux/Android, FIONREAD expects an int*, NOT an 8-byte unsigned long*!
			int nAvail = 0;
			if (ioctl(m_sock, FIONREAD, &nAvail) == 0 && nAvail > 0)
				lResult = (int32_t)nAvail;
			#endif
			}
		else
			{
			TRACE("RProtocolBSDIP::CheckReceivableBytes(): Socket is listening -- can't receive data!\n");
			}
		}
	else
		{
		TRACE("RProtocolBSDIP::CheckReceivableBytes(): Socket is not open!\n");
		}
	
	return lResult;
	}


//////////////////////////////////////////////////////////////////////////////
// check status of protocol and this socket.
//////////////////////////////////////////////////////////////////////////////
bool RProtocolBSDIP::IsError(void)
	{
	bool bResult = false;
	
	if (m_sock != INVALID_SOCKET)
		{
		#ifdef WIN32
		ms_callback = m_callback;
		ms_funcnum = RSocket::SelectFunc;
		#endif
		
		fd_set set;
		FD_ZERO(&set);
		FD_SET(m_sock, &set);
		
		TIMEVAL timeval = { 0, 0 };
		
		int32_t lCount = select(SOCK_SELECT_NFDS(m_sock), NULL, NULL, &set, &timeval);
		if (lCount > 0)
			bResult = true;
		}
	
	return bResult;
	}


//////////////////////////////////////////////////////////////////////////////
// SetCallback
//////////////////////////////////////////////////////////////////////////////
void RProtocolBSDIP::SetCallback(
	RSocket::BLOCK_CALLBACK callback)
	{
	m_callback = callback;
	}


//////////////////////////////////////////////////////////////////////////////
// GetCallback
//////////////////////////////////////////////////////////////////////////////
RSocket::BLOCK_CALLBACK RProtocolBSDIP::GetCallback(void)
	{
	return m_callback;
	}


//////////////////////////////////////////////////////////////////////////////
// GetMaxDatagramSize
//////////////////////////////////////////////////////////////////////////////
/* static */
int16_t RProtocolBSDIP::GetMaxDatagramSize(			// Returns zero on success, non-zero otherwise
	int32_t* plSize)											// Out: Maximum datagram size in bytes
	{
	int16_t sResult = 0;
	
	if (ms_bDidStartup)
		{
		#ifdef WIN32
		*plSize = (int32_t)ms_WSAData.iMaxUdpDg;
		#else
		*plSize = 65507;
		#endif
		}
	else
		{
		sResult = -1;
		TRACE("RProtocolBSDIP::GetMaxDatagramSize(): Never called Startup()!\n");
		}

	return sResult;
	}


//////////////////////////////////////////////////////////////////////////////
// Get maximum number of sockets.
//////////////////////////////////////////////////////////////////////////////
/* static */
int16_t RProtocolBSDIP::GetMaxSockets(				// Returns 0 if successfull, non-zero otherwise
	int32_t* plNum)											// Out: maximum number of sockets
	{
	int16_t sResult = 0;

	if (ms_bDidStartup)
		{
		#ifdef WIN32
		*plNum = (int32_t)ms_WSAData.iMaxSockets;
		#else
		*plNum = 256;
		#endif
		}
	else
		{
		sResult = -1;
		TRACE("RProtocolBSDIP::GetMaxSockets(): Never called Startup()!\n");
		}
	return sResult;
	}


//////////////////////////////////////////////////////////////////////////////
// Get the address of the specified host from the string provided which may be
// a name or address
//////////////////////////////////////////////////////////////////////////////
/* static */
int16_t RProtocolBSDIP::GetAddress(					// Returns 0 if successfull, non-zero otherwise
	char* pszName,											// In:  Host's name or dotted address (x.x.x.x)
	uint16_t usPort,											// In:  Host's port number
	RSocket::Address* paddress)						// Out: Address
	{
	int16_t sResult = 0;
	
	if (ms_bDidStartup)
		{
		// Trim leading/trailing whitespace that soft keyboards sometimes append
		char szClean[256];
		while (*pszName == ' ' || *pszName == '\t')
			pszName++;
		strncpy(szClean, pszName, sizeof(szClean) - 1);
		szClean[sizeof(szClean) - 1] = '\0';
		size_t len = strlen(szClean);
		while (len > 0 && (szClean[len - 1] == ' ' || szClean[len - 1] == '\t' || szClean[len - 1] == '\r' || szClean[len - 1] == '\n'))
			szClean[--len] = '\0';

		AddressIP* pip = (AddressIP*)paddress;
		memset(pip, 0, sizeof(*pip));
		
		if (len == 0)
			{
			return -1;
			}

		// Check if string is a dotted IPv4 address
		if ((strspn(szClean, "0123456789.") >= len) && (strchr(szClean, '.') != NULL))
			{
			struct in_addr inAddr;
			#ifdef WIN32
			uint32_t ulAddr = inet_addr(szClean);
			bool bValid = (ulAddr != INADDR_NONE);
			inAddr.S_un.S_addr = ulAddr;
			#else
			bool bValid = (inet_aton(szClean, &inAddr) != 0);
			#endif
			if (bValid)
				{
				pip->prototype = RSocket::TCPIP;
				pip->lAddressLen = sizeof(pip->address);
				pip->address.sin_family = AF_INET;
				pip->address.sin_addr = inAddr;
				pip->address.sin_port = htons(usPort);
				}
			else
				{
				sResult = -1;
				TRACE("RProtocolBSDIP::GetAddress(): Invalid dotted address: %s\n", szClean);
				}
			}
		else
			{
			#ifdef WIN32
			ms_funcnum = RSocket::OtherFunc;
			#endif
			HOSTENT* phost = gethostbyname(szClean);
			if (phost != NULL && phost->h_addr_list != NULL && phost->h_addr_list[0] != NULL)
				{
				pip->prototype = RSocket::TCPIP;
				pip->lAddressLen = sizeof(pip->address);
				pip->address.sin_family = AF_INET;
				memcpy((char*)&(pip->address.sin_addr), phost->h_addr_list[0],
					(size_t)phost->h_length <= sizeof(pip->address.sin_addr) ? (size_t)phost->h_length : sizeof(pip->address.sin_addr));
				pip->address.sin_port = htons(usPort);
				}
			else
				{
				sResult = -1;
				TRACE("RProtocolBSDIP::GetAddress(): Error returned by gethostbyname() for '%s'\n", szClean);
				}
			}
		}
	else
		{
		sResult = -1;
		TRACE("RProtocolBSDIP::GetAddress(): Never called Startup()!\n");
		}
	
	return sResult;
	}


////////////////////////////////////////////////////////////////////////////////
// Create broadcast address using specified port
////////////////////////////////////////////////////////////////////////////////
// static
void RProtocolBSDIP::CreateBroadcastAddress(
	uint16_t usPort,								// In:  Port to broadcast to
	RSocket::Address* paddress)						// Out: Broadcast address returned here
	{
	AddressIP* pip = (AddressIP*)paddress;
	memset(pip, 0, sizeof(*pip));
	
	pip->prototype = RSocket::TCPIP;
	pip->lAddressLen = sizeof(pip->address);
	pip->address.sin_family = AF_INET;
	pip->address.sin_port = htons(usPort);

	#ifdef WIN32
	pip->address.sin_addr.S_un.S_addr = htonl(INADDR_BROADCAST);
	#else
	pip->address.sin_addr.s_addr = htonl(INADDR_BROADCAST);
	#endif
	}


//////////////////////////////////////////////////////////////////////////////
// Get the specified address' current port number
//////////////////////////////////////////////////////////////////////////////
/* static */
uint16_t RProtocolBSDIP::GetAddressPort(	// Returns port number
	RSocket::Address* paddress)						// In:  Address from which to get port
	{
	PSOCKADDR_IN pin = (PSOCKADDR_IN) &(paddress->address);
	
	return ntohs(pin->sin_port);
	}


//////////////////////////////////////////////////////////////////////////////
// Set the specified address' port to the specified value
//////////////////////////////////////////////////////////////////////////////
/* static */
void RProtocolBSDIP::SetAddressPort(
	uint16_t usPort,											// In:  New port number
	RSocket::Address* paddress)						// I/O: Address who's port is to be set
	{
	PSOCKADDR_IN pin = (PSOCKADDR_IN) &(paddress->address);
	
	pin->sin_port = htons(usPort);
	}


////////////////////////////////////////////////////////////////////////////////
// This is a blocking-hook callback function (Win32 only).
////////////////////////////////////////////////////////////////////////////////
/* static */
#ifdef WIN32
intptr_t CALLBACK RProtocolBSDIP::BlockingHook(void)
	{
	if (ms_callback != 0)
		{
		if ((*ms_callback)() != 0)
			{
			WSACancelBlockingCall();
			}
		}
	
	return 0;
	}
#endif


//////////////////////////////////////////////////////////////////////////////
// EOF
//////////////////////////////////////////////////////////////////////////////

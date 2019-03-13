#pragma once

namespace Chat {
   class HeartbeatPongResponse;
   class UsersListResponse;
   class MessagesResponse;
   class AskForPublicKeyResponse;
   class SendOwnPublicKeyResponse;
   class LoginResponse;
   class SendMessageResponse;
   class MessageChangeStatusResponse;
   class ContactsActionResponse;
   class ChatroomsListResponse;
   class SendRoomMessageResponse;
   class RoomMessagesResponse;
   
   class ResponseHandler
   {
   public:
      virtual ~ResponseHandler() = default;
      virtual void OnHeartbeatPong(const HeartbeatPongResponse &) = 0;
      virtual void OnUsersList(const UsersListResponse &) = 0;
      virtual void OnMessages(const MessagesResponse &) = 0;
   
      // Received a call from a peer to send our public key.
      virtual void OnAskForPublicKey(const AskForPublicKeyResponse &) = 0;
      
      // Received public key of one of our peers.
      virtual void OnSendOwnPublicKey(const SendOwnPublicKeyResponse &) = 0;
   
      virtual void OnLoginReturned(const LoginResponse &) = 0;
      
      virtual void OnSendMessageResponse(const SendMessageResponse&) = 0;
      virtual void OnMessageChangeStatusResponse(const MessageChangeStatusResponse&) = 0;
      virtual void OnContactsActionResponse(const ContactsActionResponse&) = 0;
      
      virtual void OnChatroomsList(const ChatroomsListResponse&) = 0;
      virtual void OnRoomMessages(const RoomMessagesResponse&) = 0;
   };
}

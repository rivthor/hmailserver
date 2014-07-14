// Copyright (c) 2010 Martin Knafve / hMailServer.com.  
// http://www.hmailserver.com

#include "StdAfx.h"

#include "MessageIndexer.h"
#include "../BO/Message.h"
#include "../BO/MessageMetaData.h"
#include "../MIME/MIME.h"
#include "../Util/Time.h"
#include "../Persistence/PersistentMessageMetaData.h"
#include "../Persistence/PersistentMessage.h"

#ifdef _DEBUG
#define DEBUG_NEW new(_NORMAL_BLOCK, __FILE__, __LINE__)
#define new DEBUG_NEW
#endif

namespace HM
{
   MessageIndexer::MessageIndexer()
   {
      
   }

   MessageIndexer::~MessageIndexer(void)
   {

   }

   void
   MessageIndexer::Start()
   {
      boost::lock_guard<boost::recursive_mutex> guard(starterMutex_);

      if (workerThread_.joinable())
      {
         if (!workerThread_.timed_join(boost::posix_time::milliseconds(1)))
         {
            // already started.
            return;
         }
      }
      // Start the indexer now.
      LOG_DEBUG("Starting message indexing thread...");

      boost::function<void ()> func = boost::bind( &MessageIndexer::WorkerFunc, this );
      workerThread_ = boost::thread(func);

   }

   void
   MessageIndexer::WorkerFunc()
   {
      LOG_DEBUG("Indexing messages...");

      try
      {
         PersistentMessageMetaData persistentMetaData;
         persistentMetaData.DeleteOrphanedItems();

         while (true)
         {
            _IndexMessages();

            index_now_.WaitFor(chrono::minutes(1));
         }
      }
      catch (thread_interrupted const&)
      {
         // shutting down.
      }
      catch (...)
      {
         ErrorManager::Instance()->ReportError(ErrorManager::Medium, 5158, "MessageIndexer::DoWork", "An error occured while indexing messages. The indexing was aborted.");
      }
   }

   void 
   MessageIndexer::IndexNow()
   {
      index_now_.Set();
   }

   void
   MessageIndexer::Stop()
   {
      workerThread_.interrupt();
   }


   void 
   MessageIndexer::_IndexMessages()
   {
      // Default is 720
      int iIndexerFullMin = IniFileSettings::Instance()->GetIndexerFullMinutes();

      PersistentMessageMetaData persistentMetaData;
      // set quickIndex here so the loop will allow loading of an incomplete index
      bool bDoQuickIndex = true;
      if ((iIndexRunCount > iIndexerFullMin) || (iIndexRunCount == 0)){
        bDoQuickIndex = false;
        iIndexRunCount = 1;
      }

      while (true)
      {
         // added the boolean quickIndex to tell the funciton to use the quick index or the full index
         set<shared_ptr<PersistentMessageMetaData::MessageInfo> > messagesToIndex = persistentMetaData.GetMessagesToIndex(bDoQuickIndex);
         if (messagesToIndex.size() == 0)
         {
            LOG_DEBUG("No messages to index.");
            iIndexRunCount++;  //JDR
            // No messages to index found.
            return;
         }

         boost_foreach(shared_ptr<PersistentMessageMetaData::MessageInfo> messageToIndex, messagesToIndex)
         {
            boost::this_thread::interruption_point();

            AnsiString headerText = PersistentMessage::LoadHeader(messageToIndex->FileName, false);

            MimeHeader header;
            header.Load(headerText, headerText.GetLength(), true);


            String dateString = header.GetUnicodeFieldValue("Date");
            DateTime date = Time::GetDateTimeFromMimeHeader(dateString);

            String from = header.GetUnicodeFieldValue("From");
            String subject = header.GetUnicodeFieldValue("Subject");
            String cc = header.GetUnicodeFieldValue("CC");
            String to = header.GetUnicodeFieldValue("TO");

            shared_ptr<MessageMetaData> metaData = shared_ptr<MessageMetaData>(new MessageMetaData);

            metaData->SetAccountID(messageToIndex->AccountID);
            metaData->SetFolderID(messageToIndex->FolderID);
            metaData->SetMessageID(messageToIndex->MessageID);

            metaData->SetDate(date);
            metaData->SetFrom(from);
            metaData->SetSubject(subject);
            metaData->SetCC(cc);
            metaData->SetTo(to);

            if (!persistentMetaData.SaveObject(metaData))
            {
               LOG_DEBUG("Error saving the index.")
               // Error saving. Abort now...
               return;
            }
         }
      }
      iIndexRunCount++;
   }
}
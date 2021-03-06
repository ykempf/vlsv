/** This file is part of VLSV file format.
 * 
 *  Copyright 2011-2013,2015 Finnish Meteorological Institute
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <cstdlib>
#include <iostream>
#include <fstream>

#include "mpiconversion.h"
#include "vlsv_common_mpi.h"
#include "vlsv_writer.h"

using namespace std;

namespace vlsv {

   /** Constructor for Writer.*/
   Writer::Writer() {
      blockLengths = NULL;
      bytesPerProcess = NULL;
      displacements = NULL;
      dryRunning = false;
      endMultiwriteCounter = 0;
      fileOpen = false;
      initialized = false;
      multiwriteFinalized = false;
      multiwriteInitialized = false;
      multiwriteOffsetPointer = NULL;
      N_multiwriteUnits = 0;
      offset = 0;
      offsets = NULL;
      types = NULL;
      xmlWriter = NULL;
      comm = MPI_COMM_NULL;
   }

   /** Destructor for Writer. Deallocates XML writer.*/
   Writer::~Writer() {
      if (fileOpen == true) close();
      if (comm != MPI_COMM_NULL) MPI_Comm_free(&comm);
      delete [] blockLengths; blockLengths = NULL;
      delete [] bytesPerProcess; bytesPerProcess = NULL;
      delete [] displacements; displacements = NULL;
      delete [] offsets; offsets = NULL;
      delete [] types; types = NULL;
      delete xmlWriter; xmlWriter = NULL;
   }

   /** Add a multi-write unit. Function startMultiwrite must have been called 
    * by all processes prior to calling addMultiwriteUnit. The process must 
    * call endMultiwrite after it has added all multi-write units.
    * @param array Pointer to the start of data.
    * @param arrayElements Number of array elements in this multi-write unit.
    * @return If true, the multi-write unit was added successfully.
    * @see startMultiwrite
    * @see endMultiwrite.*/
   bool Writer::addMultiwriteUnit(char* array,const uint64_t& arrayElements) {
      // Check that startMultiwrite has initialized correctly:
      // addMultiwriteUnit does not call any collective MPI functions so 
      // it is safe to exit immediately here if error(s) have occurred:
      if (initialized == false) return false;
      if (multiwriteInitialized == false) return false;

      // Do not add zero-size arrays to multiWriteUnits:
      if (arrayElements == 0) return true;

      // Get the byte size of the MPI primitive datatype (MPI_INT etc.) used here:
      int datatypeBytesize;
      MPI_Type_size(getMPIDatatype(vlsvType,dataSize),&datatypeBytesize);

      // Calculate the maximum number of array elements written using a single multi-write.
      // Note: element = vector of size vectorSize, each vector element has byte size of datatypeBytesize.
      size_t maxElementsPerWrite = getMaxBytesPerWrite() / (datatypeBytesize*vectorSize);
      
      // Split the multi-write if the array has more elements than what we can 
      // write to output file using a single MPI collective:
      if (arrayElements > maxElementsPerWrite) {
         // Calculate how many collectives this process needs:
         size_t N_writes = arrayElements / maxElementsPerWrite;
         if (arrayElements % maxElementsPerWrite != 0) ++N_writes;

         // Add N_writes multi-write units:
         for (size_t i=0; i<N_writes; ++i) {
            size_t elements = maxElementsPerWrite;
            if ((i+1)*maxElementsPerWrite >= arrayElements) elements = arrayElements - i*maxElementsPerWrite;

            const size_t byteOffset = maxElementsPerWrite*vectorSize*datatypeBytesize;
            multiwriteUnits[0].push_back(Multi_IO_Unit(array+i*byteOffset,getMPIDatatype(vlsvType,dataSize),elements*vectorSize));
         }
      } else {
         multiwriteUnits[0].push_back(Multi_IO_Unit(array,getMPIDatatype(vlsvType,dataSize),arrayElements*vectorSize));
      }
      return true;
   }

   /** Close a file that has been previously opened by calling Writer::open.
    * After the file has been closed the MPI master process appends an XML footer 
    * to the end of the file, and writes an offset to the footer to the start of 
    * the file.
    * @return If true, the file was closed successfully. If false, a file may not 
    * have been opened successfully by Writer::open.*/
   bool Writer::close() {
      // If a file was never opened, exit immediately:
      if (fileOpen == false) return false;

      // Wait until all processes have finished writing data to file.
      // This is important to ensure that MPI_File_seek below will 
      // read the correct file size.
      MPI_Barrier(comm);
      
      MPI_Offset viewOffset;
      MPI_Offset endOffset;

      // Write the footer using collective MPI file operations. Only the master process 
      // actually writes something. Using collective MPI here practically eliminated 
      // all time spent here.
      if (myrank != masterRank) {
         if (dryRunning == false) {
            //Write zero length data
            MPI_File_write_at_all(fileptr,0,NULL,0,MPI_BYTE,MPI_STATUSES_IGNORE);
         }
      } else {
         if (dryRunning == false) {
            MPI_File_seek(fileptr,0,MPI_SEEK_END);
            MPI_File_get_position(fileptr,&viewOffset);
            MPI_File_get_byte_offset(fileptr,viewOffset,&endOffset);
         }

         // Print the footer to a stringstream first and then grab a 
         // pointer for writing it to the file:
         stringstream footerStream;
         xmlWriter->print(footerStream);
         string footerString = footerStream.str();
         
         double t_start = MPI_Wtime();
         if (dryRunning == false) {
            MPI_File_write_at_all(fileptr,endOffset,(char*)footerString.c_str(),footerString.size(),MPI_BYTE,MPI_STATUSES_IGNORE);
         }
         writeTime += (MPI_Wtime() - t_start);
         bytesWritten += footerStream.str().size();
      }

      // Close MPI file:
      MPI_Barrier(comm);
      if (dryRunning == false) MPI_File_close(&fileptr);

      // Master process writes footer offset to the start of file
      if (myrank == masterRank && dryRunning == false) {
         fstream footer;
         size_t footerOffset = (size_t)endOffset;
         
         footer.open(fileName.c_str(),fstream::in | fstream::out | fstream::binary | fstream::ate);
         char* ptr = reinterpret_cast<char*>(&footerOffset);
         footer.seekp(sizeof(uint64_t));
         footer.write(ptr,sizeof(uint64_t));
         footer.close();
      }

      initialized = false;
      delete [] blockLengths; blockLengths = NULL;
      delete [] bytesPerProcess; bytesPerProcess = NULL;
      delete [] displacements; displacements = NULL;
      delete [] offsets; offsets = NULL;
      delete [] types; types = NULL;
      delete xmlWriter; xmlWriter = NULL;

      // Wait for master process to finish:
      MPI_Barrier(comm);
      fileOpen = false;
      MPI_Comm_free(&comm);
      return true;
   }
   
   void Writer::endDryRunning() {
      dryRunning = false;
   }
   
   /** Get the total amount of bytes written to VLSV file. This function 
    * returns a meaningful value at master process only.
    * @return Total number of bytes written to output files by all processes.*/
   uint64_t Writer::getBytesWritten() const {return bytesWritten;}

   /** Get the time (in seconds) spent in writing the data to the output file.
    * Approximate data rate can be obtained by getBytesWritter() / getWrite().
    * @return Time spent in file I/O in seconds.*/
   double Writer::getWriteTime() const {return writeTime;}

   /** Open a VLSV file for parallel output. The file is opened on all processes 
    * in the given communicator. Additionally, master MPI process writes a 
    * header into the output file and caches a footer which will be written 
    * in Writer::close. If a file has already been opened and Writer::open 
    * is called again, the currently open file is closed before the new file is opened.
    * @param fname The name of the output file.
    * @param comm MPI communicator used in writing.
    * @param masterProcessID ID of the MPI master process.
    * @param mpiInfo MPI info, passed on to MPI_File_open.
    * @return If true, a file was opened successfully.*/
   bool Writer::open(const std::string& fname,MPI_Comm comm,const int& masterProcessID,MPI_Info mpiInfo) {
      bool success = true;
   
      // If a file with the same name has already been opened, return immediately.
      // Otherwise close the currently open file before opening the new file.
      if (fileOpen == true) {
         if (fname == fileName) return true;
         close();
      }

      MPI_Comm_dup(comm,&(this->comm));
      masterRank = masterProcessID;
      MPI_Comm_rank(this->comm,&myrank);
      MPI_Comm_size(this->comm,&N_processes);
      bytesWritten = 0;
      writeTime = 0;

      // Allocate per-thread storage:
      multiwriteOffsets.resize(1);
      multiwriteUnits.resize(1);

      // All processes in communicator comm open the same file. If a file with the 
      // given name already exists it is deleted. Note: We found out that MPI_File_open 
      // failed quite often in meteo, at least when writing many small files. It was 
      // possibly caused by MPI_File_delete call, that's the reason for the barrier.
      int accessMode = (MPI_MODE_WRONLY | MPI_MODE_CREATE);
      fileName = fname;
      if (dryRunning == false) {
         if (myrank == masterRank) MPI_File_delete(const_cast<char*>(fname.c_str()),mpiInfo);
         MPI_Barrier(comm);
         if (MPI_File_open(comm,const_cast<char*>(fileName.c_str()),accessMode,mpiInfo,&fileptr) != MPI_SUCCESS) {
            fileOpen = false;
            return fileOpen;
         }
      }

      offset = 0;           //offset set to 0 when opening a new file
      if (dryRunning == false) MPI_File_set_view(fileptr,0,MPI_BYTE,MPI_BYTE,const_cast<char*>("native"),mpiInfo);

      // Only master process needs these arrays:
      if (myrank == masterRank) {
         offsets = new MPI_Offset[N_processes];
         bytesPerProcess = new uint64_t[N_processes];    
      }
      
      // Master process opens an XML tree for storing the footer:
      if (myrank == masterRank) {
         xmlWriter     = new muxml::MuXML();
         muxml::XMLNode* root = xmlWriter->getRoot();
         xmlWriter->addNode(root,"VLSV","");
      }

      // Master writes 2 64bit integers to the start of file. 
      // Second value will be overwritten in close() function to tell 
      // the position of footer:
      if (myrank == masterRank) {
         // Write file endianness to the first byte:
         uint64_t endianness = 0;
         unsigned char* ptr = reinterpret_cast<unsigned char*>(&endianness);
         ptr[0] = detectEndianness();
         const double t_start = MPI_Wtime();
         if (dryRunning == false) {
            if (MPI_File_write_at(fileptr,0,&endianness,1,MPI_Type<uint64_t>(),MPI_STATUS_IGNORE) != MPI_SUCCESS) success = false;
            if (MPI_File_write_at(fileptr,8,&endianness,1,MPI_Type<uint64_t>(),MPI_STATUS_IGNORE) != MPI_SUCCESS) success = false;
         }
         writeTime += (MPI_Wtime() - t_start);
         offset += 2*sizeof(uint64_t); //only master rank keeps a running count
         bytesWritten += 2*sizeof(uint64_t);
      }

      // Check that everything is OK, if not then we need to close the file here. 
      // Master process needs to broadcast the status to all other processes:
      MPI_Bcast(&success,sizeof(bool),MPI_BYTE,masterRank,comm);   
      if (success == false) {
         if (dryRunning == false) {
            MPI_File_close(&fileptr);
            MPI_File_delete(const_cast<char*>(fileName.c_str()),MPI_INFO_NULL);
         }
         delete [] offsets; offsets = NULL;
         delete [] bytesPerProcess; bytesPerProcess = NULL;
         delete xmlWriter; xmlWriter = NULL;
      }

      initialized = true;
      fileOpen    = success;
      return fileOpen;
   }

   /** Resize the output file.
    * @param newSize New size.
    * @return If true, output file was successfully resized.*/
   bool Writer::setSize(MPI_Offset newSize) {
      int rvalue = MPI_File_set_size(fileptr,newSize);
      if (rvalue == MPI_SUCCESS) return true;
      return false;
   }

   /** Start dry run mode. In this mode no file I/O is performed, but getBytesWritten() 
    * will return the correct file size on master process. This can be passed to setSize function.*/
   void Writer::startDryRun() {
      dryRunning = true;
   }

   /** Start file output in multi-write mode. In multi-write mode the array write 
    * is split into multiple chunks. Typically this is done when the data in memory 
    * is not stored in a contiguous array. The multi-write mode can also be used as 
    * a workaround to issues related to integer overflows, i.e., if one wants to write 
    * more than 2^32-1 bytes of data per process. The multi-write chunks are added 
    * by calling addMultiwriteUnit functions. The data 
    * is not written to the file until endMultiwrite is called.
    * @param datatype String representation of the datatype, either 'int', 'uint', or 'float'.
    * @param arraySize Total number of array elements this process will write.
    * @param vectorSize Size of the data vector stored in array element, each process must use the same vectorSize.
    * @param dataSize Byte size of the primitive datatype, each process must use the same dataSize.
    * @return If true, multi-write mode initialized successfully.
    * @see addMultiwriteUnit
    * @see endMultiwrite.*/
   bool Writer::startMultiwrite(const string& datatype,const uint64_t& arraySize,const uint64_t& vectorSize,const uint64_t& dataSize) {
      // Check that all processes have made it this far without error(s):
      bool success = true;
      if (fileOpen == false) success = false;
      if (initialized == false) success = false;
      if (checkSuccess(success,comm) == false) return false;

      // Clear per-thread storage:
        {
           vector<list<Multi_IO_Unit> > dummy(1);
           multiwriteUnits.swap(dummy);
        }
      multiwriteOffsets[0] = numeric_limits<unsigned int>::max();

      // Array datatype and byte size of each vector element are determined 
      // from the template parameter, other values are copied from parameters:
      this->dataType   = datatype;
      this->vlsvType   = getVLSVDatatype(datatype);
      this->arraySize  = arraySize;
      this->vectorSize = vectorSize;
      this->dataSize   = dataSize;
      multiwriteFinalized = false;
      N_multiwriteUnits = 0;
      endMultiwriteCounter = 0;

      // Gather the number of bytes written by every process to MPI master process:
      myBytes = arraySize * vectorSize * dataSize;
      MPI_Gather(&myBytes,1,MPI_Type<uint64_t>(),bytesPerProcess,1,MPI_Type<uint64_t>(),masterRank,comm);

      // MPI master process calculates an offset to the output file for all processes:
      if (myrank == masterRank) {
         offsets[0] = offset;
         for (int i=1; i<N_processes; ++i) offsets[i] = offsets[i-1] + bytesPerProcess[i-1];
      }
   
      // MPI master scatters offsets:
      MPI_Scatter(offsets,1,MPI_Type<uint64_t>(),&offset,1,MPI_Type<uint64_t>(),masterRank,comm);

      multiwriteInitialized = true;
      return multiwriteInitialized;
   }

   /** Write multiwrite units to file.
    * @param tagName Name of the XML tag for this array.
    * @param attribs Attributes for the XML tag.
    * @return If true, array was successfully written to file.*/
   bool Writer::endMultiwrite(const std::string& tagName,const std::map<std::string,std::string>& attribs) {
      // Check that multiwrite mode has started successfully on all processes:
      bool success = true;
      if (initialized == false) success = false;
      if (multiwriteInitialized == false) success = false;
      if (checkSuccess(success,comm) == false) {
         multiwriteInitialized = false;
         return false;
      }
      
      // Calculate how many collective MPI calls are needed to 
      // write all the data to output file:
      size_t outputBytesize    = 0;
      size_t myCollectiveCalls = 0;
      if (multiwriteUnits[0].size() > 0) myCollectiveCalls = 1;

      vector<pair<list<Multi_IO_Unit>::iterator,list<Multi_IO_Unit>::iterator> > multiwriteList;
      list<Multi_IO_Unit>::iterator first = multiwriteUnits[0].begin();
      list<Multi_IO_Unit>::iterator last  = multiwriteUnits[0].begin();
      for (list<Multi_IO_Unit>::iterator it=multiwriteUnits[0].begin(); it!=multiwriteUnits[0].end(); ++it) {
         if (outputBytesize + (*it).amount*dataSize > getMaxBytesPerWrite()) {
            multiwriteList.push_back(make_pair(first,last));
            first = it; last = it;

            outputBytesize = 0;
            ++myCollectiveCalls;
         }
         outputBytesize += (*it).amount*dataSize;
         ++last;
      }
      multiwriteList.push_back(make_pair(first,last));

      size_t N_collectiveCalls;
      MPI_Allreduce(&myCollectiveCalls,&N_collectiveCalls,1,MPI_Type<size_t>(),MPI_MAX,comm);

      if (N_collectiveCalls > multiwriteList.size()) {
         const size_t N_dummyCalls = N_collectiveCalls-multiwriteList.size();
         for (size_t i=0; i<N_dummyCalls; ++i) {
            multiwriteList.push_back(make_pair(multiwriteUnits[0].end(),multiwriteUnits[0].end()));
         }
      }

      MPI_Offset unitOffset = 0;
      for (size_t i=0; i<multiwriteList.size(); ++i) {
         if (multiwriteFlush(i,unitOffset,multiwriteList[i].first,multiwriteList[i].second) == false) success = false;
         for (std::list<Multi_IO_Unit>::iterator it=multiwriteList[i].first; it!=multiwriteList[i].second; ++it) {
            unitOffset += it->amount*dataSize;
         }
      }

      if (multiwriteFooter(tagName,attribs) == false) success = false;
      multiwriteInitialized = false;
      if (checkSuccess(success,comm) == false) return false;
      return true;
   }

   /** Flush multi-write units to output file. This function does the actual file I/O.
    * @param counter Number of multi-write unit we are writing.
    * @param unitOffset Output file offset relative to the starting position for this process.
    * @param start Iterator pointing to the first written multi-write unit.
    * @param stop Iterator pointing past the last written multi-write unit.
    * @return If true, this process succeeded in writing out the data.*/
   bool Writer::multiwriteFlush(const size_t& counter,const MPI_Offset& unitOffset,
                                std::list<Multi_IO_Unit>::iterator& start,std::list<Multi_IO_Unit>::iterator& stop) {
      bool success = true;

      // Count the total number of multiwrite units:
      N_multiwriteUnits = 0;
      for (list<Multi_IO_Unit>::const_iterator it=start; it!=stop; ++it) {
         ++N_multiwriteUnits;
      }

      // Allocate memory for an MPI_Struct that is used to 
      // write all multiwrite units with a single collective call:
      blockLengths  = new int[N_multiwriteUnits];
      displacements = new MPI_Aint[N_multiwriteUnits];
      types         = new MPI_Datatype[N_multiwriteUnits];

      // Calculate a global offset pointer for MPI struct, i.e. an 
      // offset which is used to calculate the displacements:
      multiwriteOffsetPointer = NULL;
      if (multiwriteUnits[0].size() > 0) multiwriteOffsetPointer = start->array;

      // Copy pointers etc. to MPI struct:
      size_t i=0;
      size_t amount = 0;
      for (list<Multi_IO_Unit>::iterator it=start; it!=stop; ++it) {
         blockLengths[i]  = (*it).amount;
         displacements[i] = (*it).array - multiwriteOffsetPointer;
         types[i]         = (*it).mpiType;

         int datatypeBytesize;
         MPI_Type_size(it->mpiType,&datatypeBytesize);
         amount += (*it).amount * datatypeBytesize;
         
         ++i;
      }

      // Write data to file:
      if (dryRunning == false) {
         if (N_multiwriteUnits > 0) {
            // Create an MPI struct containing the multiwrite units:
            MPI_Datatype outputType;
            MPI_Type_create_struct(N_multiwriteUnits,blockLengths,displacements,types,&outputType);
            MPI_Type_commit(&outputType);

            // Write data to output file with a single collective call:
            const double t_start = MPI_Wtime();
            MPI_File_write_at_all(fileptr,offset+unitOffset,multiwriteOffsetPointer,1,outputType,MPI_STATUS_IGNORE);
            writeTime += (MPI_Wtime() - t_start);
            MPI_Type_free(&outputType);
         } else {
            // Process has no data to write but needs to participate in the collective call to prevent deadlock:
            const double t_start = MPI_Wtime();
            MPI_File_write_at_all(fileptr,offset+unitOffset,NULL,0,MPI_BYTE,MPI_STATUS_IGNORE);
            writeTime += (MPI_Wtime() - t_start);
         }
      }

      // Deallocate memory:
      delete [] blockLengths; blockLengths = NULL;
      delete [] displacements; displacements = NULL;
      delete [] types; types = NULL;

      return success;
   }

   /** Insert an entry to the XML footer that is kept in memory. This 
    * function only has an effect on the master process.
    * @param tagName Name of the array that was written to output file.
    * @param attribs Attributes that are given to the new footer entry.
    * @return If true, footer entry was inserted successfully.*/
   bool Writer::multiwriteFooter(const std::string& tagName,const std::map<std::string,std::string>& attribs) {
      bool success = true;
      if (myrank != masterRank) return true;

      // Count total number of bytes written to file:
      uint64_t totalBytes = 0;
      for (int i=0; i<N_processes; ++i) totalBytes += bytesPerProcess[i];

      muxml::XMLNode* root = xmlWriter->getRoot();
      muxml::XMLNode* xmlnode = xmlWriter->find("VLSV",root);
      muxml::XMLNode* node = xmlWriter->addNode(xmlnode,tagName,offset);
      for (map<string,string>::const_iterator it=attribs.begin(); it!=attribs.end(); ++it) {
         xmlWriter->addAttribute(node,it->first,it->second);
      }
      xmlWriter->addAttribute(node,"vectorsize",vectorSize);
      xmlWriter->addAttribute(node,"arraysize",totalBytes/dataSize/vectorSize);
      xmlWriter->addAttribute(node,"datatype",dataType);
      xmlWriter->addAttribute(node,"datasize",dataSize);

      // Update global file offset:
      offset += totalBytes;
      bytesWritten += totalBytes;

      return success;
   }

   bool Writer::writeArray(const std::string& arrayName,const std::map<std::string,std::string>& attribs,const std::string& dataType,
                           const uint64_t& arraySize,const uint64_t& vectorSize,const uint64_t& dataSize,const char* array) {
      // Check that everything is OK before continuing:
      bool success = true;
      if (initialized == false) success = false;
      if (fileOpen == false) success = false;
      if (checkSuccess(success,comm) == false) return false;

      if (startMultiwrite(dataType,arraySize,vectorSize,dataSize) == false) {
         success = false; return success;
      }

      char* arrayPtr = const_cast<char*>(array);
      if (addMultiwriteUnit(arrayPtr,arraySize) == false) success = false;
      if (checkSuccess(success,comm) == false) {
         return false;
      }

      if (endMultiwrite(arrayName,attribs) == false) success = false;
      return success;
   }

} // namespace vlsv

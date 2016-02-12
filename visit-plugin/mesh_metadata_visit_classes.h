/** This file is part of VLSV file format.
 * 
 *  Copyright 2016 Arto Sandroos
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

/** This file provides VisIt wrappers for MeshMetadata classes. 
 *  Typically the wrapper functions call the same function in MeshMetadata 
 *  or in one of its subclasses, and optionally write additional debugging 
 *  information to VisIt log files.*/

#pragma once

#ifndef MESH_METADATA_VISIT_CLASSES_H
#define MESH_METADATA_VISIT_CLASSES_H

#include <mesh_metadata_visit.h>
#include <mesh_metadata_visit_point.h>
#include <mesh_metadata_visit_ucd_multi.h>
#include <mesh_metadata_visit_quad_multi.h>
#include <mesh_metadata_visit_ucd_amr.h>
#include <mesh_metadata_visit_ucd_generic_multi.h>

namespace vlsvplugin {

   class VisitPointMeshMetadata: public PointMeshMetadata,public VisitMeshMetadata {
   public:
      VisitPointMeshMetadata();
      virtual ~VisitPointMeshMetadata();

      virtual avtMeshType getAvtMeshType() const;
      virtual std::string getAvtMeshTypeString() const;

   protected:
      virtual const std::string& getCorrectVlsvMeshType() const;
      virtual bool readVariables(vlsv::Reader* vlsv,const std::map<std::string,std::string>& attribs);
   };

   class VisitUCDMultiMeshMetadata: public UCDMultiMeshMetadata,public VisitMeshMetadata {
   public:
      VisitUCDMultiMeshMetadata();
      virtual ~VisitUCDMultiMeshMetadata();

      virtual avtMeshType getAvtMeshType() const;
      virtual std::string getAvtMeshTypeString() const;

   protected:
      virtual const std::string& getCorrectVlsvMeshType() const;
      virtual bool readDomains(vlsv::Reader* vlsvReader);
      virtual bool readVariables(vlsv::Reader* vlsv,const std::map<std::string,std::string>& attribs);
   };

   class VisitQuadMultiMeshMetadata: public QuadMultiMeshMetadata,public VisitMeshMetadata {
   public:
      VisitQuadMultiMeshMetadata();
      virtual ~VisitQuadMultiMeshMetadata();

      virtual avtMeshType getAvtMeshType() const;
      virtual std::string getAvtMeshTypeString() const;
      virtual bool getDomainInfo(vlsv::Reader* vlsvReader,int domain,const uint64_t*& domainOffsets,
			                     const uint64_t*& ghostOffsets,const uint64_t*& variableOffsets);
      virtual bool read(vlsv::Reader* vlsvReader,const std::map<std::string,std::string>& attribs);

   protected:
      virtual const std::string& getCorrectVlsvMeshType() const;
      virtual bool readDomains(vlsv::Reader* vlsvReader);
      virtual bool readVariables(vlsv::Reader* vlsv,const std::map<std::string,std::string>& attribs);
   };

   class VisitUCDAMRMetadata: public UCDAMRMetadata,public VisitMeshMetadata {
   public:
      VisitUCDAMRMetadata();
      virtual ~VisitUCDAMRMetadata();

      virtual avtMeshType getAvtMeshType() const;
      virtual std::string getAvtMeshTypeString() const;
      virtual bool getDomainInfo(vlsv::Reader* vlsvReader,int domain,const uint64_t*& domainOffsets,
			                     const uint64_t*& ghostOffsets,const uint64_t*& variableOffsets);
      virtual bool read(vlsv::Reader* vlsvReader,const std::map<std::string,std::string>& attribs);

   protected:
      virtual const std::string& getCorrectVlsvMeshType() const;
      virtual bool readDomains(vlsv::Reader* vlsvReader);
      virtual bool readVariables(vlsv::Reader* vlsv,const std::map<std::string,std::string>& attribs);
   };

   class VisitUCDGenericMultiMeshMetadata: public UCDGenericMultiMeshMetadata,public VisitMeshMetadata {
   public:
      VisitUCDGenericMultiMeshMetadata();
      virtual ~VisitUCDGenericMultiMeshMetadata();

      virtual avtMeshType getAvtMeshType() const;
      virtual std::string getAvtMeshTypeString() const;
      virtual const uint64_t getCellConnectivitySize(int domain) const;
      virtual bool getDomainInfo(vlsv::Reader* vlsvReader,int domain,const uint64_t*& domainOffsets,
					             const uint64_t*& ghostOffsets,const uint64_t*& variableOffsets);
      virtual bool getDomainInfoNodes(vlsv::Reader* vlsvReader,int domain,const uint64_t*& domainOffsets,
			                          const uint64_t*& ghostOffsets,const uint64_t*& variableOffsets);
      virtual bool getDomainInfoZones(vlsv::Reader* vlsvReader,int domain,const uint64_t*& domainOffsets,
			                          const uint64_t*& ghostOffsets,const uint64_t*& variableOffsets);
      virtual int getNodeDataSize() const;
      virtual vlsv::datatype::type getNodeDatatype() const;
      virtual bool read(vlsv::Reader* vlsv,const std::map<std::string,std::string>& attribs);

   protected:
      virtual const std::string& getCorrectVlsvMeshType() const;
      virtual bool readDomains(vlsv::Reader* vlsvReader);
      virtual bool readVariables(vlsv::Reader* vlsv,const std::map<std::string,std::string>& attribs);
   };

} // namespace vlsvplugin

#endif
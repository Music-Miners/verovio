/////////////////////////////////////////////////////////////////////////////
// Name:        chord.cpp
// Author:      Andrew Horwitz
// Created:     2015
// Copyright (c) Authors and others. All rights reserved.
/////////////////////////////////////////////////////////////////////////////

#include "chord.h"

//----------------------------------------------------------------------------

#include <assert.h>
#include <iostream>

//----------------------------------------------------------------------------

#include "note.h"

namespace vrv {
    
//----------------------------------------------------------------------------
// Chord
//----------------------------------------------------------------------------

Chord::Chord( ):
LayerElement("chord-"), ObjectListInterface(), DurationInterface(),
    AttColoration(),
    AttCommon(),
    AttStemmed(),
    AttTiepresent()
{
    Reset();
    m_drawingStemDir = STEMDIRECTION_NONE;
    m_ledgerLines[0][0] = 0;
    m_ledgerLines[0][1] = 0;
    m_ledgerLines[1][0] = 0;
    m_ledgerLines[1][1] = 0;
}

Chord::~Chord()
{
    ClearClusters();
}

void Chord::Reset()
{
    ClearClusters();
    DocObject::Reset();
    DurationInterface::Reset();
    ResetCommon();
    ResetStemmed();
    ResetColoration();
    ResetTiepresent();
}
    
void Chord::ClearClusters()
{
    std::list<ChordCluster*>::iterator iter;
    for (iter = m_clusters.begin(); iter != m_clusters.end(); ++iter)
    {
        ChordCluster *cluster = dynamic_cast<ChordCluster*>(*iter);
        for (std::vector<Note*>::iterator clIter = cluster->begin(); clIter != cluster->end(); ++clIter)
        {
            Note *note = dynamic_cast<Note*>(*clIter);
            note->m_cluster = NULL;
            note->m_clusterPosition = 0;
        }
        delete *iter;
    }
    m_clusters.clear();
}
    
void Chord::AddLayerElement(vrv::LayerElement *element)
{
    assert( dynamic_cast<Note*>(element) );
    element->SetParent( this );
    m_children.push_back(element);
    Modify();
}
    
bool compare_pitch (Object *first, Object *second)
{
    Note *n1 = dynamic_cast<Note*>(first);
    Note *n2 = dynamic_cast<Note*>(second);
    return ( n1->GetDiatonicPitch() < n2->GetDiatonicPitch() );
}

void Chord::FilterList( ListOfObjects *childList )
{
    // Retain only note children of chords
    ListOfObjects::iterator iter = childList->begin();
    
    while ( iter != childList->end()) {
        LayerElement *currentElement = dynamic_cast<LayerElement*>(*iter);
        if ( !currentElement ) {
            // remove anything that is not an LayerElement
            iter = childList->erase( iter );
        }
        else if ( !currentElement->HasDurationInterface() )
        {
            iter = childList->erase( iter );
        }
        else /*if ( dynamic_cast<EditorialElement*>(currentElement))
        {
            Object* object = currentElement->GetFirstChild(&typeid(Note));
            if (dynamic_cast<Note*>(object))
            {
                iter++;
            }
        }
        else */{
            Note *n = dynamic_cast<Note*>(currentElement);
            
            if (n) {
                iter++;
            } else {
                // if it is not a note, drop it
                iter = childList->erase( iter );
            }
        }
    }
    
    childList->sort(compare_pitch);
    
    iter = childList->begin();
    
    this->ClearClusters();
    
    Note *curNote, *lastNote = dynamic_cast<Note*>(*iter);
    int curPitch, lastPitch = lastNote->GetDiatonicPitch();
    ChordCluster* curCluster = NULL;
    
    iter++;
    
    while ( iter != childList->end()) {
        curNote = dynamic_cast<Note*>(*iter);
        curPitch = curNote->GetDiatonicPitch();
        
        if (curPitch - lastPitch == 1) {
            if(!lastNote->m_cluster)
            {
                curCluster = new ChordCluster();
                m_clusters.push_back(curCluster);
                curCluster->push_back(lastNote);
                lastNote->m_cluster = curCluster;
                lastNote->m_clusterPosition = (int)curCluster->size();
            }
            curCluster->push_back(curNote);
            curNote->m_cluster = curCluster;
            curNote->m_clusterPosition = (int)curCluster->size();
        }
        
        lastNote = curNote;
        lastPitch = curPitch;
        
        iter++;
    }
}

void Chord::ResetAccidList()
{
    m_accidList.clear();
    ListOfObjects* childList = this->GetList(this); //make sure it's initialized
    for (ListOfObjects::reverse_iterator it = childList->rbegin(); it != childList->rend(); it++) {
        Note *note = dynamic_cast<Note*>(*it);
        if (note->HasAccid()) {
            m_accidList.push_back(note);
        }
    }
}
    
void Chord::ResetAccidSpace(int fullUnit)
{
    m_accidSpace.clear();
 
    if (m_accidList.size() == 0) return;
    
    int halfUnit = fullUnit / 2;
    int doubleUnit = fullUnit * 2;
    
    //make m_accidSpace into a 2D vector of size (vertical half-units, most possible horizontal halfunits)
    int idx, setIdx;
    int size = (int)m_accidList.size();
    std::vector<bool> *accidLine;
    //top y position - bottom y position in half-units
    int rows = ((m_accidList[0]->GetDrawingY() - m_accidList[m_accidList.size() - 1]->GetDrawingY()) / halfUnit);
    m_accidSpace.resize(std::max(rows, ACCID_WIDTH));
    
    //each line needs to be 4 times the number of notes in case every one overlaps fully
    int lineLength = (doubleUnit*size) / halfUnit;
    for(idx = 0; idx < m_accidSpace.size(); idx++)
    {
        accidLine = &m_accidSpace.at(idx);
        //resize each line
        accidLine->resize(lineLength);
        //initialize all spaces to false
        for(setIdx = 0; setIdx < lineLength; setIdx++) accidLine->at(setIdx) = false;
    }
}
    
void Chord::GetYExtremes(int *yMax, int *yMin)
{
    bool passed = false;
    int y1;
    ListOfObjects* childList = this->GetList(this); //make sure it's initialized
    for (ListOfObjects::iterator it = childList->begin(); it != childList->end(); it++) {
        Note *note = dynamic_cast<Note*>(*it);
        if (!note) continue;
        y1 = note->GetDrawingY();
        if (!passed) {
            *yMax = y1;
            *yMin = y1;
            passed = true;
        }
        else {
            if (y1 > *yMax) *yMax = y1;
            else if (y1 < *yMin) *yMin = y1;
        }
    }
}

//----------------------------------------------------------------------------
// Functors methods
//----------------------------------------------------------------------------

int Chord::PrepareTieAttr( ArrayPtrVoid params )
{
    // param 0: std::vector<Note*>* that holds the current notes with open ties (unused)
    // param 1: Chord** currentChord for the current chord if in a chord
    Chord **currentChord = static_cast<Chord**>(params[1]);
    
    assert(!(*currentChord));
    (*currentChord) = this;

    return FUNCTOR_CONTINUE;
}

int Chord::PrepareTieAttrEnd( ArrayPtrVoid params )
{
    // param 0: std::vector<Note*>* that holds the current notes with open ties (unused)
    // param 1: Chord** currentChord for the current chord if in a chord
    Chord **currentChord = static_cast<Chord**>(params[1]);
    
    assert((*currentChord));
    (*currentChord) = NULL;
    
    return FUNCTOR_CONTINUE;
}
    
}
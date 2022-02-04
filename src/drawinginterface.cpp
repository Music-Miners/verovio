/////////////////////////////////////////////////////////////////////////////
// Name:        drawinginterface.cpp
// Author:      Laurent Pugin
// Created:     2015
// Copyright (c) Authors and others. All rights reserved.
/////////////////////////////////////////////////////////////////////////////

#include "drawinginterface.h"

//----------------------------------------------------------------------------

#include <assert.h>

//----------------------------------------------------------------------------

#include "chord.h"
#include "elementpart.h"
#include "layerelement.h"
#include "note.h"
#include "object.h"
#include "staff.h"

namespace vrv {

// helper for determining note direction
data_STEMDIRECTION GetNoteDirection(int leftNoteY, int rightNoteY)
{
    if (leftNoteY == rightNoteY) return STEMDIRECTION_NONE;
    return (leftNoteY < rightNoteY) ? STEMDIRECTION_up : STEMDIRECTION_down;
}

//----------------------------------------------------------------------------
// DrawingListInterface
//----------------------------------------------------------------------------

DrawingListInterface::DrawingListInterface()
{
    this->Reset();
}

DrawingListInterface::~DrawingListInterface() {}

void DrawingListInterface::Reset()
{
    m_drawingList.clear();
}

void DrawingListInterface::AddToDrawingList(Object *object)
{
    if (std::find(m_drawingList.begin(), m_drawingList.end(), object) == m_drawingList.end()) {
        // someName not in name, add it
        m_drawingList.push_back(object);
    }

    /*
    m_drawingList.push_back(object);
    m_drawingList.sort();
    m_drawingList.unique();
     */
}

ArrayOfObjects *DrawingListInterface::GetDrawingList()
{
    return &m_drawingList;
}

void DrawingListInterface::ResetDrawingList()
{
    m_drawingList.clear();
}

//----------------------------------------------------------------------------
// BeamDrawingInterface
//----------------------------------------------------------------------------

BeamDrawingInterface::BeamDrawingInterface() : ObjectListInterface()
{
    this->Reset();
}

BeamDrawingInterface::~BeamDrawingInterface()
{
    ClearCoords();
}

void BeamDrawingInterface::Reset()
{
    m_changingDur = false;
    m_beamHasChord = false;
    m_hasMultipleStemDir = false;
    m_cueSize = false;
    m_fractionSize = 100;
    m_crossStaffContent = NULL;
    m_crossStaffRel = STAFFREL_basic_NONE;
    m_shortestDur = 0;
    m_notesStemDir = STEMDIRECTION_NONE;
    m_drawingPlace = BEAMPLACE_NONE;
    m_beamStaff = NULL;

    m_beamWidth = 0;
    m_beamWidthBlack = 0;
    m_beamWidthWhite = 0;
}

int BeamDrawingInterface::GetTotalBeamWidth() const
{
    return m_beamWidthBlack + (m_shortestDur - DUR_8) * m_beamWidth;
}

void BeamDrawingInterface::ClearCoords()
{
    ArrayOfBeamElementCoords::iterator iter;
    for (iter = m_beamElementCoords.begin(); iter != m_beamElementCoords.end(); ++iter) {
        delete *iter;
    }
    m_beamElementCoords.clear();
}

void BeamDrawingInterface::InitCoords(ArrayOfObjects *childList, Staff *staff, data_BEAMPLACE place)
{
    assert(staff);

    BeamDrawingInterface::Reset();
    ClearCoords();

    if (childList->empty()) {
        return;
    }

    m_beamStaff = staff;

    // duration variables
    int lastDur, currentDur;

    m_beamElementCoords.reserve(childList->size());
    int i;
    for (i = 0; i < (int)childList->size(); ++i) {
        m_beamElementCoords.push_back(new BeamElementCoord());
    }

    // current point to the first Note in the layed out layer
    LayerElement *current = dynamic_cast<LayerElement *>(childList->front());
    // Beam list should contain only DurationInterface objects
    assert(current->GetDurationInterface());

    lastDur = (current->GetDurationInterface())->GetActualDur();

    /******************************************************************/
    // Populate BeamElementCoord for each element in the beam
    // This could be moved to Beam::InitCoord for optimization because there should be no
    // need for redoing it everytime it is drawn.

    data_STEMDIRECTION currentStemDir;
    Layer *layer = NULL;

    int elementCount = 0;

    ArrayOfObjects::iterator iter = childList->begin();
    do {
        // Beam list should contain only DurationInterface objects
        assert(current->GetDurationInterface());
        currentDur = (current->GetDurationInterface())->GetActualDur();

        if (current->Is(CHORD)) {
            m_beamHasChord = true;
        }

        m_beamElementCoords.at(elementCount)->m_element = current;
        m_beamElementCoords.at(elementCount)->m_dur = currentDur;

        // Look at beam breaks
        m_beamElementCoords.at(elementCount)->m_breaksec = 0;
        AttBeamSecondary *beamsecondary = dynamic_cast<AttBeamSecondary *>(current);
        if (beamsecondary && beamsecondary->HasBreaksec()) {
            if (!m_changingDur) m_changingDur = true;
            m_beamElementCoords.at(elementCount)->m_breaksec = beamsecondary->GetBreaksec();
        }

        Staff *staff = current->GetCrossStaff(layer);
        if (staff && (staff != m_beamStaff)) {
            m_crossStaffContent = staff;
            m_crossStaffRel = current->GetCrossStaffRel();
        }
        // Check if some beam chord has cross staff content
        else if (current->Is(CHORD)) {
            Chord *chord = vrv_cast<Chord *>(current);
            for (Note *note : { chord->GetTopNote(), chord->GetBottomNote() }) {
                if (note->m_crossStaff && (note->m_crossStaff != m_beamStaff)) {
                    m_crossStaffContent = note->m_crossStaff;
                    m_crossStaffRel = note->GetCrossStaffRel();
                }
            }
        }

        // Skip rests and tabGrp
        if (current->Is({ CHORD, NOTE })) {
            // Look at the stemDir to see if we have multiple stem Dir
            if (!m_hasMultipleStemDir) {
                // At this stage, BeamCoord::m_stem is not necessary set, so we need to look at the Note / Chord
                // original value Example: IsInBeam called in Note::PrepareLayerElementParts when reaching the first
                // note of the beam
                currentStemDir = m_beamElementCoords.at(elementCount)->GetStemDir();
                if (currentStemDir != STEMDIRECTION_NONE) {
                    if ((m_notesStemDir != STEMDIRECTION_NONE) && (m_notesStemDir != currentStemDir)) {
                        m_hasMultipleStemDir = true;
                        m_notesStemDir = STEMDIRECTION_NONE;
                    }
                    else {
                        m_notesStemDir = currentStemDir;
                    }
                }
            }
        }
        // Skip rests
        if (current->Is({ CHORD, NOTE, TABGRP })) {
            // keep the shortest dur in the beam
            m_shortestDur = std::max(currentDur, m_shortestDur);
        }

        // check if we have more than duration in the beam
        if (!m_changingDur && currentDur != lastDur) m_changingDur = true;
        lastDur = currentDur;

        elementCount++;

        ++iter;
        if (iter == childList->end()) {
            break;
        }
        current = dynamic_cast<LayerElement *>(*iter);
        if (current == NULL) {
            LogDebug("Error accessing element in Beam list");
            return;
        }

    } while (1);

    // elementCount must be greater than 0 here
    if (elementCount == 0) {
        LogDebug("Beam with no notes of duration > 8 detected. Exiting DrawBeam.");
        return;
    }
}

void BeamDrawingInterface::InitCue(bool beamCue)
{
    if (beamCue) {
        m_cueSize = beamCue;
    }
    else {
        m_cueSize = std::all_of(m_beamElementCoords.begin(), m_beamElementCoords.end(), [](BeamElementCoord *coord) {
            if (!coord->m_element) return false;
            if (coord->m_element->IsGraceNote() || coord->m_element->GetDrawingCueSize()) return true;
            return false;
        });
    }

    // Always set stem direction to up for grace note beam unless stem direction is provided
    if (m_cueSize && (m_notesStemDir == STEMDIRECTION_NONE)) {
        m_notesStemDir = STEMDIRECTION_up;
    }
}

bool BeamDrawingInterface::IsHorizontal()
{
    if (this->IsRepeatedPattern()) {
        return true;
    }

    if (this->HasOneStepHeight()) return true;

    // if (m_drawingPlace == BEAMPLACE_mixed) return true;

    if (m_drawingPlace == BEAMPLACE_NONE) return true;

    int elementCount = (int)m_beamElementCoords.size();

    std::vector<int> items;
    std::vector<data_BEAMPLACE> directions;
    items.reserve(m_beamElementCoords.size());
    directions.reserve(m_beamElementCoords.size());

    for (int i = 0; i < elementCount; ++i) {
        BeamElementCoord *coord = m_beamElementCoords.at(i);
        if (!coord->m_stem || !coord->m_closestNote) continue;

        items.push_back(coord->m_closestNote->GetDrawingY());
        directions.push_back(coord->m_beamRelativePlace);
    }
    int itemCount = (int)items.size();

    if (itemCount < 2) return true;

    const int first = items.front();
    const int last = items.back();

    // First note and last note have the same postion
    if (first == last) return true;

    // If drawing place is mixed and is should be drawn horizontal based on mixed rules
    if ((m_drawingPlace == BEAMPLACE_mixed) && (this->IsHorizontalMixedBeam(items, directions))) return true;

    // Detect beam with two pitches only and as step at the beginning or at the end
    const bool firstStep = (first != items.at(1));
    const bool lastStep = (last != items.at(items.size() - 2));
    if ((items.size() > 2) && (firstStep || lastStep)) {
        // Detect concave shapes
        for (int i = 1; i < itemCount - 1; ++i) {
            if (m_drawingPlace == BEAMPLACE_above) {
                if ((items.at(i) >= first) && (items.at(i) >= last)) return true;
            }
            else if (m_drawingPlace == BEAMPLACE_below) {
                if ((items.at(i) <= first) && (items.at(i) <= last)) return true;
            }
        }
        std::vector<int> pitches;
        std::unique_copy(items.begin(), items.end(), std::back_inserter(pitches));
        if (pitches.size() == 2) {
            if (m_drawingPlace == BEAMPLACE_above) {
                // Single note at the beginning as lower first
                if (firstStep && (std::is_sorted(items.begin(), items.end()))) return true;
                // Single note at the end and lower last
                if (lastStep && (std::is_sorted(items.rbegin(), items.rend()))) return true;
            }
            else {
                // Single note at the end and higher last
                if (lastStep && (std::is_sorted(items.begin(), items.end()))) return true;
                // Single note at the beginning and higher first
                if (firstStep && (std::is_sorted(items.rbegin(), items.rend()))) return true;
            }
        }
    }

    return false;
}

bool BeamDrawingInterface::IsHorizontalMixedBeam(
    const std::vector<int> &items, const std::vector<data_BEAMPLACE> &directions) const
{
    // items and directions should be of the same size, otherwise something is wrong
    if (items.size() != directions.size()) return false;
    if ((items.size() == 3) && m_crossStaffContent) {
        if ((directions.at(0) == directions.at(2)) && (directions.at(0) != directions.at(1))) return true;
    }

    // calculate how many times stem direction is changed withing the beam
    int directionChanges = 0;
    data_BEAMPLACE previous = directions.front();
    std::for_each(directions.begin(), directions.end(), [&previous, &directionChanges](data_BEAMPLACE current) {
        if (current != previous) {
            ++directionChanges;
            previous = current;
        }
    });
    // if we have a mix of cross-staff elements, going from one staff to another repeatedly, we need to check note
    // directions. Otherwise we can use direction of the outside pitches for beam
    if (directionChanges <= 1) return false;

    int previousTop = VRV_UNSET;
    int previousBottom = VRV_UNSET;
    data_STEMDIRECTION outsidePitchDirection = GetNoteDirection(items.front(), items.back());
    std::map<data_STEMDIRECTION, int> beamDirections{ { STEMDIRECTION_NONE, 0 }, { STEMDIRECTION_up, 0 },
        { STEMDIRECTION_down, 0 } };
    for (int i = 0; i < (int)items.size(); ++i) {
        if (directions[i] == BEAMPLACE_above) {
            if (previousTop == VRV_UNSET) {
                previousTop = items[i];
            }
            else {
                ++beamDirections[GetNoteDirection(previousTop, items[i])];
            }
        }
        else if (directions[i] == BEAMPLACE_below) {
            if (previousBottom == VRV_UNSET) {
                previousBottom = items[i];
            }
            else {
                ++beamDirections[GetNoteDirection(previousBottom, items[i])];
            }
        }
    }
    // if direction of beam outside pitches corresponds to majority of the note directions within the beam, beam
    // can be drawn in that direction. Otherwise horizontal beam should be used
    bool result = std::any_of(
        beamDirections.begin(), beamDirections.end(), [&beamDirections, &outsidePitchDirection](const auto &pair) {
            return (pair.first == outsidePitchDirection) ? false : pair.second > beamDirections[outsidePitchDirection];
        });
    return result;
}

bool BeamDrawingInterface::IsRepeatedPattern()
{
    if (m_drawingPlace == BEAMPLACE_mixed) return false;

    if (m_drawingPlace == BEAMPLACE_NONE) return false;

    int elementCount = (int)m_beamElementCoords.size();

    // No pattern with at least 4 elements
    if (elementCount < 4) return false;

    std::vector<int> items;
    items.reserve(m_beamElementCoords.size());

    int i;
    for (i = 0; i < elementCount; ++i) {
        BeamElementCoord *coord = m_beamElementCoords.at(i);
        if (!coord->m_stem || !coord->m_closestNote) continue;

        // Could this be an overflow with 32 bits?
        items.push_back(coord->m_closestNote->GetDrawingY() * DUR_MAX + coord->m_dur);
    }
    int itemCount = (int)items.size();

    // No pattern with at least 4 elements or if all elements are the same
    if ((itemCount < 4) || (std::equal(items.begin() + 1, items.end(), items.begin()))) {
        return false;
    }

    // Find all possible dividers for the sequence (without 1 and its size)
    std::vector<int> dividers;
    for (i = 2; i <= itemCount / 2; ++i) {
        if (itemCount % i == 0) dividers.push_back(i);
    }

    // Correlate a sub-array for each divider until a sequence is found (if any)
    for (i = 0; i < (int)dividers.size(); ++i) {
        int divider = dividers.at(i);
        int j;
        bool pattern = true;
        std::vector<int>::iterator iter = items.begin();
        std::vector<int> v1 = std::vector<int>(iter, iter + divider);
        for (j = 1; j < (itemCount / divider); ++j) {
            std::vector<int> v2 = std::vector<int>(iter + j * divider, iter + (j + 1) * divider);
            if (v1 != v2) {
                pattern = false;
                break;
            }
        }
        if (pattern) {
            // LogDebug("Pattern found %d", divider);
            return true;
        }
    }

    return false;
}

bool BeamDrawingInterface::HasOneStepHeight()
{
    if (m_shortestDur < DUR_32) return false;

    int top = -128;
    int bottom = 128;
    for (auto coord : m_beamElementCoords) {
        if (coord->m_closestNote) {
            Note *note = vrv_cast<Note *>(coord->m_closestNote);
            assert(note);
            int loc = note->GetDrawingLoc();
            if (loc > top) top = loc;
            if (loc < bottom) bottom = loc;
        }
    }

    return (abs(top - bottom) <= 1);
}

bool BeamDrawingInterface::IsFirstIn(Object *object, LayerElement *element)
{
    this->GetList(object);
    int position = this->GetPosition(object, element);
    // This method should be called only if the note is part of a fTrem
    assert(position != -1);
    // this is the first one
    if (position == 0) return true;
    return false;
}

bool BeamDrawingInterface::IsLastIn(Object *object, LayerElement *element)
{
    int size = (int)this->GetList(object)->size();
    int position = this->GetPosition(object, element);
    // This method should be called only if the note is part of a beam
    assert(position != -1);
    // this is the last one
    if (position == (size - 1)) return true;
    return false;
}

int BeamDrawingInterface::GetPosition(Object *object, LayerElement *element)
{
    this->GetList(object);
    int position = this->GetListIndex(element);
    // Check if this is a note in the chord
    if ((position == -1) && (element->Is(NOTE))) {
        Note *note = vrv_cast<Note *>(element);
        assert(note);
        Chord *chord = note->IsChordTone();
        if (chord) position = this->GetListIndex(chord);
    }
    return position;
}

void BeamDrawingInterface::GetBeamOverflow(StaffAlignment *&above, StaffAlignment *&below)
{

    if (!m_beamStaff || !m_crossStaffContent) return;

    if (m_drawingPlace == BEAMPLACE_mixed) {
        above = NULL;
        below = NULL;
    }
    // Beam below - ignore above and find the appropriate below staff
    else if (m_drawingPlace == BEAMPLACE_below) {
        above = NULL;
        if (m_crossStaffRel == STAFFREL_basic_above) {
            below = m_beamStaff->GetAlignment();
        }
        else {
            below = m_crossStaffContent->GetAlignment();
        }
    }
    // Beam above - ignore below and find the appropriate above staff
    else if (m_drawingPlace == BEAMPLACE_above) {
        below = NULL;
        if (m_crossStaffRel == STAFFREL_basic_below) {
            above = m_beamStaff->GetAlignment();
        }
        else {
            above = m_crossStaffContent->GetAlignment();
        }
    }
}

void BeamDrawingInterface::GetBeamChildOverflow(StaffAlignment *&above, StaffAlignment *&below)
{
    if (m_beamStaff && m_crossStaffContent) {
        if (m_crossStaffRel == STAFFREL_basic_above) {
            above = m_crossStaffContent->GetAlignment();
            below = m_beamStaff->GetAlignment();
        }
        else {
            above = m_beamStaff->GetAlignment();
            below = m_crossStaffContent->GetAlignment();
        }
    }
}

//----------------------------------------------------------------------------
// StaffDefDrawingInterface
//----------------------------------------------------------------------------

StaffDefDrawingInterface::StaffDefDrawingInterface()
{
    this->Reset();
}

StaffDefDrawingInterface::~StaffDefDrawingInterface() {}

void StaffDefDrawingInterface::Reset()
{
    m_currentClef.Reset();
    m_currentKeySig.Reset();
    m_currentMensur.Reset();
    m_currentMeterSig.Reset();
    m_currentMeterSigGrp.Reset();

    m_drawClef = false;
    m_drawKeySig = false;
    m_drawMensur = false;
    m_drawMeterSig = false;
    m_drawMeterSigGrp = false;
}

void StaffDefDrawingInterface::SetCurrentClef(Clef const *clef)
{
    if (clef) {
        m_currentClef = *clef;
        m_currentClef.CloneReset();
    }
}

void StaffDefDrawingInterface::SetCurrentKeySig(KeySig const *keySig)
{
    if (keySig) {
        char drawingCancelAccidCount = m_currentKeySig.GetAccidCount();
        data_ACCIDENTAL_WRITTEN drawingCancelAccidType = m_currentKeySig.GetAccidType();
        m_currentKeySig = *keySig;
        m_currentKeySig.CloneReset();
        m_currentKeySig.m_drawingCancelAccidCount = drawingCancelAccidCount;
        m_currentKeySig.m_drawingCancelAccidType = drawingCancelAccidType;
    }
}

void StaffDefDrawingInterface::SetCurrentMensur(Mensur const *mensur)
{
    if (mensur) {
        m_currentMensur = *mensur;
        m_currentMensur.CloneReset();
    }
}

void StaffDefDrawingInterface::SetCurrentMeterSig(MeterSig const *meterSig)
{
    if (meterSig) {
        m_currentMeterSig = *meterSig;
        m_currentMeterSig.CloneReset();
    }
}

void StaffDefDrawingInterface::SetCurrentMeterSigGrp(MeterSigGrp const *meterSigGrp)
{
    if (meterSigGrp) {
        m_currentMeterSigGrp = *meterSigGrp;
        m_currentMeterSigGrp.CloneReset();
    }
}

bool StaffDefDrawingInterface::DrawMeterSigGrp()
{
    if (m_drawMeterSigGrp) {
        const ArrayOfObjects *childList = m_currentMeterSigGrp.GetList(&m_currentMeterSigGrp);
        if (childList->size() > 1) return true;
    }
    return false;
}

void StaffDefDrawingInterface::AlternateCurrentMeterSig(Measure *measure)
{
    if (MeterSigGrp *meterSigGrp = this->GetCurrentMeterSigGrp();
        meterSigGrp->GetFunc() == meterSigGrpLog_FUNC_alternating) {
        meterSigGrp->SetMeasureBasedCount(measure);
        MeterSig *meter = meterSigGrp->GetSimplifiedMeterSig();
        this->SetCurrentMeterSig(meter);
        delete meter;
    }
}

//----------------------------------------------------------------------------
// StemmedDrawingInterface
//----------------------------------------------------------------------------

StemmedDrawingInterface::StemmedDrawingInterface()
{
    this->Reset();
}

StemmedDrawingInterface::~StemmedDrawingInterface() {}

void StemmedDrawingInterface::Reset()
{
    m_drawingStem = NULL;
}

void StemmedDrawingInterface::SetDrawingStem(Stem *stem)
{
    m_drawingStem = stem;
}

void StemmedDrawingInterface::SetDrawingStemDir(data_STEMDIRECTION stemDir)
{
    if (m_drawingStem) m_drawingStem->SetDrawingStemDir(stemDir);
}

data_STEMDIRECTION StemmedDrawingInterface::GetDrawingStemDir()
{
    if (m_drawingStem) return m_drawingStem->GetDrawingStemDir();
    return STEMDIRECTION_NONE;
}

void StemmedDrawingInterface::SetDrawingStemLen(int stemLen)
{
    if (m_drawingStem) m_drawingStem->SetDrawingStemLen(stemLen);
}

int StemmedDrawingInterface::GetDrawingStemLen()
{
    if (m_drawingStem) return m_drawingStem->GetDrawingStemLen();
    return 0;
}

Point StemmedDrawingInterface::GetDrawingStemStart(Object *object)
{
    assert(m_drawingStem || object);
    if (object && !m_drawingStem) {
        assert(this == dynamic_cast<StemmedDrawingInterface *>(object));
        return Point(object->GetDrawingX(), object->GetDrawingY());
    }
    return Point(m_drawingStem->GetDrawingX(), m_drawingStem->GetDrawingY());
}

Point StemmedDrawingInterface::GetDrawingStemEnd(Object *object)
{
    assert(m_drawingStem || object);
    if (object && !m_drawingStem) {
        assert(this == dynamic_cast<StemmedDrawingInterface *>(object));
        if (!m_drawingStem) {
            // Somehow arbitrary for chord - stem end it the bottom with no stem
            if (object->Is(CHORD)) {
                Chord *chord = vrv_cast<Chord *>(object);
                assert(chord);
                return Point(object->GetDrawingX(), chord->GetYBottom());
            }
            return Point(object->GetDrawingX(), object->GetDrawingY());
        }
    }
    return Point(m_drawingStem->GetDrawingX(), m_drawingStem->GetDrawingY() - this->GetDrawingStemLen());
}

} // namespace vrv

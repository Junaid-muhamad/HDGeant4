/**************************************************************************                                                                                                                           
* HallD software                                                          * 
* Copyright(C) 2020       GlueX and KLF Collaborations                    * 
*                                                                         *                                                                                                                               
* Author: The GlueX and KLF Collaborations                                *                                                                                                                                
* Contributors: Igal Jaegle                                               *                                                                                                                               
*                                                                         *                                                                                                                               
* This software is provided "as is" without any warranty.                 *
**************************************************************************/

#include "GlueXSensitiveDetectorCGEM.hh"
#include "GlueXDetectorConstruction.hh"
#include "GlueXPrimaryGeneratorAction.hh"
#include "GlueXUserEventInformation.hh"
#include "GlueXUserTrackInformation.hh"

#include "G4VPhysicalVolume.hh"
#include "G4PVPlacement.hh"
#include "G4EventManager.hh"
#include "G4HCofThisEvent.hh"
#include "G4Step.hh"
#include "G4SDManager.hh"
#include "G4ios.hh"

#include <JANA/JApplication.h>

// Cutoff on the total number of allowed hits
int GlueXSensitiveDetectorCGEM::MAX_HITS = 100;

// Minimum hit time difference for two hits on the same hole
double GlueXSensitiveDetectorCGEM::TWO_HIT_TIME_RESOL = 400*ns;

// Minimum photoelectron count for a hit
double GlueXSensitiveDetectorCGEM::THRESH_KEV = 0.;

int GlueXSensitiveDetectorCGEM::instanceCount = 0;
G4Mutex GlueXSensitiveDetectorCGEM::fMutex = G4MUTEX_INITIALIZER;

GlueXSensitiveDetectorCGEM::GlueXSensitiveDetectorCGEM(const G4String& name)
 : G4VSensitiveDetector(name),
   fHoleHitsMap(0), fPointsMap(0)
{
   collectionName.insert("CGEMHoleHitsCollection");
   collectionName.insert("CGEMPointsCollection");

   // The rest of this only needs to happen once, the first time an object
   // of this type is instantiated for this configuration of geometry and
   // fields. If the geometry or fields change in such a way as to modify
   // the drift-time properties of hits in the CGEM, you must delete all old
   // objects of this class and create new ones.

   G4AutoLock holerier(&fMutex);
   if (instanceCount++ == 0) {
      extern int run_number;
      extern jana::JApplication *japp;
      if (japp == 0) {
         G4cerr << "Error in GlueXSensitiveDetector constructor - "
                << "jana global DApplication object not set, "
                << "cannot continue." << G4endl;
         exit(-1);
      }
      jana::JCalibration *jcalib = japp->GetJCalibration(run_number);
      if (japp == 0) {   // dummy
         jcalib = 0;
         G4cout << "CGEM: ALL parameters loaded from ccdb" << G4endl;
      }
   }
}

GlueXSensitiveDetectorCGEM::GlueXSensitiveDetectorCGEM(
                     const GlueXSensitiveDetectorCGEM &src)
 : G4VSensitiveDetector(src),
   fHoleHitsMap(src.fHoleHitsMap), fPointsMap(src.fPointsMap)
{
   G4AutoLock holerier(&fMutex);
   ++instanceCount;
}

GlueXSensitiveDetectorCGEM &GlueXSensitiveDetectorCGEM::operator=(const
                                         GlueXSensitiveDetectorCGEM &src)
{
   G4AutoLock holerier(&fMutex);
   *(G4VSensitiveDetector*)this = src;
   fHoleHitsMap = src.fHoleHitsMap;
   fPointsMap = src.fPointsMap;
   return *this;
}

GlueXSensitiveDetectorCGEM::~GlueXSensitiveDetectorCGEM() 
{
   G4AutoLock holerier(&fMutex);
   --instanceCount;
}

void GlueXSensitiveDetectorCGEM::Initialize(G4HCofThisEvent* hce)
{
   fHoleHitsMap = new
              GlueXHitsMapCGEMhole(SensitiveDetectorName, collectionName[0]);
   fPointsMap = new
              GlueXHitsMapCGEMpoint(SensitiveDetectorName, collectionName[1]);
   G4SDManager *sdm = G4SDManager::GetSDMpointer();
   hce->AddHitsCollection(sdm->GetCollectionID(collectionName[0]), fHoleHitsMap);
   hce->AddHitsCollection(sdm->GetCollectionID(collectionName[1]), fPointsMap);
}

G4bool GlueXSensitiveDetectorCGEM::ProcessHits(G4Step* step, 
                                                G4TouchableHistory* ROhist)
{
   double dEsum = step->GetTotalEnergyDeposit();
   if (dEsum == 0)
      return false;

   const G4ThreeVector &pin = step->GetPreStepPoint()->GetMomentum();
   const G4ThreeVector &xin = step->GetPreStepPoint()->GetPosition();
   const G4ThreeVector &xout = step->GetPostStepPoint()->GetPosition();
   double Ein = step->GetPreStepPoint()->GetTotalEnergy();
   double tin = step->GetPreStepPoint()->GetGlobalTime();
   double tout = step->GetPostStepPoint()->GetGlobalTime();
   G4ThreeVector x = (xin + xout) / 2;
   G4ThreeVector dx = xout - xin;
   double t = (tin + tout) / 2;

   const G4VTouchable* touch = step->GetPreStepPoint()->GetTouchable();
   const G4AffineTransform &local_from_global = touch->GetHistory()
                                                     ->GetTopTransform();
   G4ThreeVector xlocal = local_from_global.TransformPoint(x);
  
   // For particles that range out inside the active volume, the
   // "out" time may sometimes be set to something enormously high.
   // This screws up the hit. Check for this case here by looking
   // at tout and making sure it is less than 1 second. If it's
   // not, then just use tin for "t".

   if (tout > 1.0*s)
      t = tin;

   // Post the hit to the points list in the
   // order of appearance in the event simulation.

   G4Track *track = step->GetTrack();
   G4int trackID = track->GetTrackID();
   GlueXUserTrackInformation *trackinfo = (GlueXUserTrackInformation*)
                                          track->GetUserInformation();
   int itrack = trackinfo->GetGlueXTrackID();
   if (trackinfo->GetGlueXHistory() == 0 && itrack > 0 && xin.dot(pin) > 0) {
      G4int key = fPointsMap->entries();
      GlueXHitCGEMpoint* lastPoint = (*fPointsMap)[key - 1];
      if (lastPoint == 0 || lastPoint->track_ != trackID ||
          fabs(lastPoint->t_ns - t/ns) > 0.1 ||
          fabs(lastPoint->x_cm - x[0]/cm) > 2. ||
          fabs(lastPoint->y_cm - x[1]/cm) > 2. ||
          fabs(lastPoint->z_cm - x[2]/cm) > 2.)
      {
         int pdgtype = track->GetDynamicParticle()->GetPDGcode();
         int g3type = GlueXPrimaryGeneratorAction::ConvertPdgToGeant3(pdgtype);
         GlueXHitCGEMpoint newPoint;
         newPoint.ptype_G3 = g3type;
         newPoint.track_ = trackID;
         newPoint.trackID_ = itrack;
         newPoint.primary_ = (track->GetParentID() == 0);
         newPoint.t_ns = t/ns;
         newPoint.x_cm = x[0]/cm;
         newPoint.y_cm = x[1]/cm;
         newPoint.z_cm = x[2]/cm;
         newPoint.px_GeV = pin[0]/GeV;
         newPoint.py_GeV = pin[1]/GeV;
         newPoint.pz_GeV = pin[2]/GeV;
         newPoint.E_GeV = Ein/GeV;
         fPointsMap->add(key, newPoint);
      }
   }

   // Post the hit to the hits map, ordered by plane,hole,end index

   if (dEsum > 0) {

      // HDDS geometry does not include holes nor plane rotations. Assume 1 cm
      // hole spacing with holes at 0.5cm on either side of the beamline at
      // x,y = 0,0. Also assume odd number planes have holes in the vertical
      // direction and even numbered planes have holes in the horizontal direction.
      // Vertical holes start with hole 1 at x=-71.5 and hole 144 at x=+71.5
      // (the gas volume ends at x=+/-72.0). Horizontal holes start with hole 1
      // at y=-71.5 (i.e. closest to the ground) and hole 144 at y=+71.5
      // (i.e. closest to the sky).

      int layer = GetIdent("layer", touch);
      int hole = 0;
      if (layer % 2 != 0) {
         // Vertical holes
         hole = floor(x[0] + 73.0);
      }
      else {
         // Horizontal holes
         hole = floor(x[1] + 73.0);
      }
      
      if (hole < 1 || hole > 144)
         return false;
      
      int key = GlueXHitCGEMhole::GetKey(layer, hole);
      GlueXHitCGEMhole *counter = (*fHoleHitsMap)[key];
      if (counter == 0) {
         GlueXHitCGEMhole newhole(layer, hole);
         fHoleHitsMap->add(key, newhole);
         counter = (*fHoleHitsMap)[key];
      }

      // Add the hit to the hits vector, maintaining strict time ordering

      int merge_hit = 0;
      std::vector<GlueXHitCGEMhole::hitinfo_t>::iterator hiter;
      for (hiter = counter->hits.begin(); hiter != counter->hits.end(); ++hiter) {
         if (fabs(hiter->t_ns*ns - t) < TWO_HIT_TIME_RESOL) {
            merge_hit = 1;
            break;
         }
         else if (hiter->t_ns*ns > t) {
            break;
         }
         if (merge_hit) {
            // Use the time from the earlier hit but add the charge
            hiter->dE_keV += dEsum/keV;
            hiter->dx_cm += dx.mag()/cm;
            if (hiter->t_ns*ns > t) {
               hiter->t_ns = t/ns;
            }
         }
         else if ((int)counter->hits.size() < MAX_HITS)	{
            // create new hit 
            hiter = counter->hits.insert(hiter, GlueXHitCGEMhole::hitinfo_t());
            hiter->dE_keV = dEsum/keV;
            hiter->dx_cm = dx.mag()/cm;
            hiter->t_ns = t/ns;
         }
         else {
            G4cerr << "GlueXSensitiveDetectorCGEM::ProcessHits error: "
                << "max hit count " << MAX_HITS
                << " exceeded, truncating!"
                << G4endl;
         }
      }
   }
   return true;
}

void GlueXSensitiveDetectorCGEM::EndOfEvent(G4HCofThisEvent*)
{
   std::map<int,GlueXHitCGEMhole*> *holes = fHoleHitsMap->GetMap();
   std::map<int,GlueXHitCGEMpoint*> *points = fPointsMap->GetMap();
   if (holes->size() == 0 && points->size() == 0)
      return;
   std::map<int,GlueXHitCGEMhole*>::iterator siter;
   std::map<int,GlueXHitCGEMpoint*>::iterator piter;

   if (verboseLevel > 1) { 
      G4cout << G4endl
             << "--------> Hits Collection: in this event there are "
             << holes->size() << " holes with hits in the CGEM: "
             << G4endl;
      for (siter = holes->begin(); siter != holes->end(); ++siter)
         siter->second->Print();

      G4cout << G4endl
             << "--------> Hits Collection: in this event there are "
             << points->size() << " truth points in the CGEM: "
             << G4endl;
      for (piter = points->begin(); piter != points->end(); ++piter)
         piter->second->Print();
   }

   // pack hits into ouptut hddm record
 
   G4EventManager* mgr = G4EventManager::GetEventManager();
   G4VUserEventInformation* info = mgr->GetUserInformation();
   hddm_s::HDDM *record = ((GlueXUserEventInformation*)info)->getOutputRecord();
   if (record == 0) {
      G4cerr << "GlueXSensitiveDetectorCGEM::EndOfEvent error - "
             << "hits seen but no output hddm record to save them into, "
             << "cannot continue!" << G4endl;
      exit(1);
   }

   if (record->getPhysicsEvents().size() == 0) 
      record->addPhysicsEvents();
   if (record->getHitViews().size() == 0) 
      record->getPhysicsEvent().addHitViews();
   hddm_s::HitView &hitview = record->getPhysicsEvent().getHitView();
   if (hitview.getCGEMs().size() == 0)
      hitview.addCGEMs();
      hddm_s::CGEM &cgem = hitview.getCGEM();

   // Collect and output the cgemTruthHits
   for (siter = holes->begin(); siter != holes->end(); ++siter) {
      std::vector<GlueXHitCGEMhole::hitinfo_t> &hits = siter->second->hits;
      // apply a pulse height threshold cut
      for (unsigned int ih=0; ih < hits.size(); ++ih) {
         if (hits[ih].dE_keV <= THRESH_KEV) {
            hits.erase(hits.begin() + ih);
            --ih;
         }
      }
      if (hits.size() > 0) {
	hddm_s::CGemList hole = cgem.addCGems(1);
         hole(0).setLayer(siter->second->layer_);
         hole(0).setHole(siter->second->hole_);
         for (int ih=0; ih < (int)hits.size(); ++ih) {
            hddm_s::CGemTruthHitList thit = hole(0).addCGemTruthHits(1);
            thit(0).setDE(hits[ih].dE_keV);
            thit(0).setDx(hits[ih].dx_cm);
            thit(0).setT(hits[ih].t_ns);
	    }
      }
   }

   // Collect and output the cgemTruthPoints
   for (piter = points->begin(); piter != points->end(); ++piter) {
     hddm_s::CGemTruthPointList point = cgem.addCGemTruthPoints(1);
      point(0).setPrimary(piter->second->primary_);
      point(0).setPtype(piter->second->ptype_G3);
      point(0).setPx(piter->second->px_GeV);
      point(0).setPy(piter->second->py_GeV);
      point(0).setPz(piter->second->pz_GeV);
      point(0).setE(piter->second->E_GeV);
      point(0).setX(piter->second->x_cm);
      point(0).setY(piter->second->y_cm);
      point(0).setZ(piter->second->z_cm);
      point(0).setT(piter->second->t_ns);
      point(0).setTrack(piter->second->track_);
      hddm_s::TrackIDList tid = point(0).addTrackIDs();
      tid(0).setItrack(piter->second->trackID_);
   }
}

int GlueXSensitiveDetectorCGEM::GetIdent(std::string div, 
                                        const G4VTouchable *touch)
{
   const HddsG4Builder* bldr = GlueXDetectorConstruction::GetBuilder();
   std::map<std::string, std::vector<int> >::const_iterator iter;
   std::map<std::string, std::vector<int> > *identifiers;
   int max_depth = touch->GetHistoryDepth();
   for (int depth = 0; depth < max_depth; ++depth) {
      G4VPhysicalVolume *pvol = touch->GetVolume(depth);
      G4LogicalVolume *lvol = pvol->GetLogicalVolume();
      int volId = fVolumeTable[lvol];
      if (volId == 0) {
         volId = bldr->getVolumeId(lvol);
         fVolumeTable[lvol] = volId;
      }
      identifiers = &Refsys::fIdentifierTable[volId];
      if ((iter = identifiers->find(div)) != identifiers->end()) {
         int copyNum = touch->GetCopyNumber(depth);
         copyNum += (dynamic_cast<G4PVPlacement*>(pvol))? -1 : 0;
         return iter->second[copyNum];
      }
   }
   return -1;
}

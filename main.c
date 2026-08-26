#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

#define VISITOR_CSV     "Visitor.csv"
#define VEHICLE_CSV     "Vehicle.csv"
#define HOST_CSV        "Host.csv"
#define VISIT_CSV       "Visit.csv"
#define ACCESSCODE_CSV  "AccessCode.csv"
#define GATE_CSV        "Gate.csv"
#define MOVEMENT_CSV    "Movement.csv"
#define INCIDENT_CSV    "Incident.csv"

#define LOG_FILE        "db_log.csv"

#define HASH_SIZE  1009

#define S_NATID   21
#define S_FNAME   51
#define S_LNAME   51
#define S_PHONE   21
#define S_EMAIL   101

#define S_PLATE   16
#define S_TYPE    21
#define S_COLOR   21
#define S_MANUF   31
#define S_MODEL   31

#define S_ORG     101

#define S_PURPOSE 51
#define S_DEST    101
#define S_DATE    11      /* YYYY-MM-DD */
#define S_TIME    9       /* HH:MM:SS */
#define S_STATUS  16
#define S_DT      20      /* YYYY-MM-DD HH:MM:SS */

#define S_CODE    21
#define S_CODEST  11

#define S_GATENAME 51

#define S_INCTYPE  21
#define S_DESC     256
#define S_INCST    11

#define LINE_BUF   1024

/*
Structs (Tables)
*/

/* 1) Visitor */
typedef struct {
    int  VisitorID;                 /* PK */
    char NationalID[S_NATID];        /* UNIQUE */
    char FirstName[S_FNAME];
    char LastName[S_LNAME];
    char Phone[S_PHONE];
    char Email[S_EMAIL];             /* NULL allowed -> empty string means NULL */
} Visitor;

/* 2) Vehicle */
typedef struct {
    int  VehicleID;                 /* PK */
    int  VisitorID;                 /* FK -> Visitor */
    char PlateNumber[S_PLATE];      /* UNIQUE */
    char Type[S_TYPE];
    char Color[S_COLOR];
    char Manufacturer[S_MANUF];
    char Model[S_MODEL];
} Vehicle;

/* 3) Host */
typedef struct {
    int  HostID;                    /* PK */
    char FirstName[S_FNAME];
    char LastName[S_LNAME];
    char Organization[S_ORG];
    char Phone[S_PHONE];
    char Email[S_EMAIL];            /* UNIQUE */
} Host;

/* 4) Visit */
typedef struct {
    int  VisitID;                   /* PK */
    int  VisitorID;                 /* FK -> Visitor */
    int  HostID;                    /* FK -> Host */
    char Purpose[S_PURPOSE];
    char Destination[S_DEST];
    char ScheduledDate[S_DATE];     /* DATE */
    char StartTime[S_TIME];         /* TIME */
    char EndTime[S_TIME];           /* TIME */
    char Status[S_STATUS];          /* Pending/Approved/... */
    char CreatedAt[S_DT];           /* NOT NULL */
    char ApprovedAt[S_DT];          /* NULL allowed -> empty string means NULL */
} Visit;

/* 5) AccessCode */
typedef struct {
    int  AccessCodeID;              /* PK */
    int  VisitID;                   /* FK -> Visit, UNIQUE (one per visit) */
    char CodeValue[S_CODE];         /* UNIQUE */
    char IssuedAt[S_DT];
    char ValidFrom[S_DT];
    char ValidTo[S_DT];
    char CodeStatus[S_CODEST];      /* Active/Revoked/Expired */
} AccessCode;

/* 6) Gate */
typedef struct {
    int  GateID;                    /* PK */
    char GateName[S_GATENAME];      /* UNIQUE */
} Gate;

/* 7) Movement */
typedef struct {
    int  MovementID;                /* PK */
    int  VisitID;                   /* FK -> Visit, UNIQUE (one per visit) */
    int  AccessCodeID;              /* FK -> AccessCode, UNIQUE (one per code) */
    int  VehicleID;                 /* FK -> Vehicle */
    int  EntryGateID;               /* FK -> Gate, NULL allowed -> -1 */
    int  ExitGateID;                /* FK -> Gate, NULL allowed -> -1 */
    char EntryTime[S_DT];           /* NULL allowed -> "" */
    char ExitTime[S_DT];            /* NULL allowed -> "" */
} Movement;

/* 8) Incident */
typedef struct {
    int  IncidentID;                /* PK */
    int  VisitID;                   /* FK -> Visit */
    char IncidentType[S_INCTYPE];   /* Overstay/InvalidCode/... */
    char Description[S_DESC];
    char ReportedAt[S_DT];
    char IncidentStatus[S_INCST];   /* Open/Resolved */
} Incident;

/*
Global Tables (Dynamic Arrays)
 */
typedef struct { Visitor    *rows; int count; int cap; } VisitorTable;
typedef struct { Vehicle    *rows; int count; int cap; } VehicleTable;
typedef struct { Host       *rows; int count; int cap; } HostTable;
typedef struct { Visit      *rows; int count; int cap; } VisitTable;
typedef struct { AccessCode *rows; int count; int cap; } AccessCodeTable;
typedef struct { Gate       *rows; int count; int cap; } GateTable;
typedef struct { Movement   *rows; int count; int cap; } MovementTable;
typedef struct { Incident   *rows; int count; int cap; } IncidentTable;

static VisitorTable    gVisitors;
static VehicleTable    gVehicles;
static HostTable       gHosts;
static VisitTable       gVisits;
static AccessCodeTable gAccessCodes;
static GateTable       gGates;
static MovementTable   gMovements;
static IncidentTable   gIncidents;

/*
Hash Indexing (PK -> row index)
*/
typedef struct DataItem {
    int key;
    int data; /* row index */
} DataItem;

static DataItem* dummyItem = NULL;

static DataItem* visitorHash[HASH_SIZE];
static DataItem* vehicleHash[HASH_SIZE];
static DataItem* hostHash[HASH_SIZE];
static DataItem* visitHash[HASH_SIZE];
static DataItem* accessCodeHash[HASH_SIZE];
static DataItem* gateHash[HASH_SIZE];
static DataItem* movementHash[HASH_SIZE];
static DataItem* incidentHash[HASH_SIZE];

static int currentUserID = 0;

static pthread_mutex_t gLogMutex = PTHREAD_MUTEX_INITIALIZER;


/*
Utility Helpers
*/
static void safeCopy(char *dst, int dstSize, const char *src) {
    if (!dst || dstSize <= 0) return;
    if (!src) src = "";
    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
}

static void trimNewline(char *s) {
    if (!s) return;
    int n = (int)strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r')) {
        s[n-1] = '\0';
        n--;
    }
}

static char* csvField(char **cursor) {
    if (!cursor || !(*cursor)) return (char*)"";
    char *start = *cursor;
    char *p = start;

    while (*p && *p != ',' && *p != '\n' && *p != '\r') p++;

    if (*p == ',') {
        *p = '\0';
        *cursor = p + 1;
    } else {
        *p = '\0';
        *cursor = NULL;
    }
    return start;
}

static int toIntSafe(const char *s, int nullValue) {
    if (!s || s[0] == '\0') return nullValue;
    return atoi(s);
}

/* Compare times (HH:MM or HH:MM:SS) by converting to seconds */
static int timeToSeconds(const char *t) {
    int hh = 0, mm = 0, ss = 0;
    if (!t || t[0] == '\0') return -1;
    if (sscanf(t, "%d:%d:%d", &hh, &mm, &ss) < 2) return -1;
    return hh*3600 + mm*60 + ss;
}

static void* ensureCapacity(void *arr, int *cap, int needed, size_t elemSize) {
    if (needed <= *cap) return arr;
    int newCap = (*cap == 0) ? 10 : *cap;
    while (newCap < needed) newCap *= 2;
    void *newArr = realloc(arr, (size_t)newCap * elemSize);
    if (!newArr) {
        printf("ERROR: Out of memory.\n");
        exit(1);
    }
    *cap = newCap;
    return newArr;
}

static int hashCode(int key) {
    if (key < 0) key = -key;
    return key % HASH_SIZE;
}

static void hashInit(DataItem* hashArr[]) {
    for (int i = 0; i < HASH_SIZE; i++) hashArr[i] = NULL;
}

static void hashInsert(DataItem* hashArr[], int key, int data) {
    int hashIndex = hashCode(key);

    while (hashArr[hashIndex] != NULL &&
           hashArr[hashIndex]->key != -1 &&
           hashArr[hashIndex]->key != key) {
        hashIndex++;
        hashIndex %= HASH_SIZE;
    }

    if (hashArr[hashIndex] != NULL && hashArr[hashIndex]->key == key) {
        hashArr[hashIndex]->data = data;
        return;
    }

    DataItem *item = (DataItem*)malloc(sizeof(DataItem));
    if (!item) { printf("ERROR: Out of memory.\n"); exit(1); }
    item->key = key;
    item->data = data;
    hashArr[hashIndex] = item;
}

static int hashSearch(DataItem* hashArr[], int key) {
    int hashIndex = hashCode(key);
    int startIndex = hashIndex;

    while (hashArr[hashIndex] != NULL) {
        if (hashArr[hashIndex]->key == key) return hashArr[hashIndex]->data;
        hashIndex++;
        hashIndex %= HASH_SIZE;
        if (hashIndex == startIndex) break;
    }
    return -1;
}

static void hashFreeItems(DataItem* hashArr[]) {
    for (int i = 0; i < HASH_SIZE; i++) {
        if (hashArr[i] != NULL && hashArr[i] != dummyItem) {
            free(hashArr[i]);
        }
        hashArr[i] = NULL;
    }
}

static void rebuildVisitorIndex(void) {
    hashFreeItems(visitorHash);
    for (int i = 0; i < gVisitors.count; i++) hashInsert(visitorHash, gVisitors.rows[i].VisitorID, i);
}
static void rebuildVehicleIndex(void) {
    hashFreeItems(vehicleHash);
    for (int i = 0; i < gVehicles.count; i++) hashInsert(vehicleHash, gVehicles.rows[i].VehicleID, i);
}
static void rebuildHostIndex(void) {
    hashFreeItems(hostHash);
    for (int i = 0; i < gHosts.count; i++) hashInsert(hostHash, gHosts.rows[i].HostID, i);
}
static void rebuildVisitIndex(void) {
    hashFreeItems(visitHash);
    for (int i = 0; i < gVisits.count; i++) hashInsert(visitHash, gVisits.rows[i].VisitID, i);
}
static void rebuildAccessCodeIndex(void) {
    hashFreeItems(accessCodeHash);
    for (int i = 0; i < gAccessCodes.count; i++) hashInsert(accessCodeHash, gAccessCodes.rows[i].AccessCodeID, i);
}
static void rebuildGateIndex(void) {
    hashFreeItems(gateHash);
    for (int i = 0; i < gGates.count; i++) hashInsert(gateHash, gGates.rows[i].GateID, i);
}
static void rebuildMovementIndex(void) {
    hashFreeItems(movementHash);
    for (int i = 0; i < gMovements.count; i++) hashInsert(movementHash, gMovements.rows[i].MovementID, i);
}
static void rebuildIncidentIndex(void) {
    hashFreeItems(incidentHash);
    for (int i = 0; i < gIncidents.count; i++) hashInsert(incidentHash, gIncidents.rows[i].IncidentID, i);
}

/*
Logging Functions
*/

static void setCurrentUserID(int userId) {
    currentUserID = userId;
}

static void getCurrentDateTime(char *buffer, int size) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buffer, (size_t)size, "%Y-%m-%d %H:%M:%S", t);
}

static void writeHeaderIfNeeded(void) {
    FILE *fp = fopen(LOG_FILE, "r");
    if (fp == NULL) {
        fp = fopen(LOG_FILE, "w");
        if (fp != NULL) {
            fprintf(fp, "LogID,TableName,ColumnName,ID,OldValue,NewValue,ActionType,UserID,LogDate\n");
        }
    }
    if (fp) fclose(fp);
}

static int getNextLogID(void) {
    FILE *fp = fopen(LOG_FILE, "r");
    int id = 1;
    char line[LINE_BUF];

    if (fp == NULL) return id;

    /* skip header */
    if (fgets(line, sizeof(line), fp) == NULL) {
        fclose(fp);
        return id;
    }
    while (fgets(line, sizeof(line), fp) != NULL) id++;
    fclose(fp);
    return id;
}

static void writeLog(const char *tableName,
                     const char *columnName,
                     int recordId,
                     const char *oldValue,
                     const char *newValue,
                     const char *actionType) {
    writeHeaderIfNeeded();

    FILE *fp = fopen(LOG_FILE, "a");
    if (fp == NULL) {
        printf("WARNING: Could not open log file.\n");
        return;
    }

    int logId = getNextLogID();
    char dt[S_DT];
    getCurrentDateTime(dt, sizeof(dt));

    if (!oldValue) oldValue = "";
    if (!newValue) newValue = "";

    fprintf(fp, "%d,%s,%s,%d,%s,%s,%s,%d,%s\n",
            logId, tableName, columnName, recordId, oldValue, newValue, actionType, currentUserID, dt);

    fclose(fp);
}

/*
LOAD / SAVE (Storage Manager)
*/

/* ---- Visitor ---- */
static void saveVisitors(void) {
    FILE *fp = fopen(VISITOR_CSV, "w");
    if (!fp) { printf("ERROR: Cannot write %s\n", VISITOR_CSV); return; }

    fprintf(fp, "VisitorID,NationalID,FirstName,LastName,Phone,Email\n");
    for (int i = 0; i < gVisitors.count; i++) {
        Visitor *v = &gVisitors.rows[i];
        fprintf(fp, "%d,%s,%s,%s,%s,%s\n",
                v->VisitorID, v->NationalID, v->FirstName, v->LastName, v->Phone, v->Email);
    }
    fclose(fp);
}

static void loadVisitors(void) {
    FILE *fp = fopen(VISITOR_CSV, "r");
    if (!fp) return;

    char line[LINE_BUF];

    fgets(line, sizeof(line), fp);

    gVisitors.count = 0;

    while (fgets(line, sizeof(line), fp)) {
        trimNewline(line);
        char *cur = line;

        Visitor v;
        memset(&v, 0, sizeof(v));

        v.VisitorID = toIntSafe(csvField(&cur), -1);
        safeCopy(v.NationalID, sizeof(v.NationalID), csvField(&cur));
        safeCopy(v.FirstName, sizeof(v.FirstName), csvField(&cur));
        safeCopy(v.LastName, sizeof(v.LastName), csvField(&cur));
        safeCopy(v.Phone, sizeof(v.Phone), csvField(&cur));
        safeCopy(v.Email, sizeof(v.Email), csvField(&cur));

        gVisitors.rows = (Visitor*)ensureCapacity(gVisitors.rows, &gVisitors.cap,
                                                 gVisitors.count + 1, sizeof(Visitor));
        gVisitors.rows[gVisitors.count++] = v;
    }

    fclose(fp);
}

/* ---- Vehicle ---- */
static void saveVehicles(void) {
    FILE *fp = fopen(VEHICLE_CSV, "w");
    if (!fp) { printf("ERROR: Cannot write %s\n", VEHICLE_CSV); return; }

    fprintf(fp, "VehicleID,VisitorID,PlateNumber,Type,Color,Manufacturer,Model\n");
    for (int i = 0; i < gVehicles.count; i++) {
        Vehicle *v = &gVehicles.rows[i];
        fprintf(fp, "%d,%d,%s,%s,%s,%s,%s\n",
                v->VehicleID, v->VisitorID, v->PlateNumber, v->Type, v->Color, v->Manufacturer, v->Model);
    }
    fclose(fp);
}

static void loadVehicles(void) {
    FILE *fp = fopen(VEHICLE_CSV, "r");
    if (!fp) return;

    char line[LINE_BUF];
    fgets(line, sizeof(line), fp); /* header */

    gVehicles.count = 0;

    while (fgets(line, sizeof(line), fp)) {
        trimNewline(line);
        char *cur = line;

        Vehicle v;
        memset(&v, 0, sizeof(v));

        v.VehicleID = toIntSafe(csvField(&cur), -1);
        v.VisitorID = toIntSafe(csvField(&cur), -1);
        safeCopy(v.PlateNumber, sizeof(v.PlateNumber), csvField(&cur));
        safeCopy(v.Type, sizeof(v.Type), csvField(&cur));
        safeCopy(v.Color, sizeof(v.Color), csvField(&cur));
        safeCopy(v.Manufacturer, sizeof(v.Manufacturer), csvField(&cur));
        safeCopy(v.Model, sizeof(v.Model), csvField(&cur));

        gVehicles.rows = (Vehicle*)ensureCapacity(gVehicles.rows, &gVehicles.cap,
                                                 gVehicles.count + 1, sizeof(Vehicle));
        gVehicles.rows[gVehicles.count++] = v;
    }

    fclose(fp);
}

/* ---- Host ---- */
static void saveHosts(void) {
    FILE *fp = fopen(HOST_CSV, "w");
    if (!fp) { printf("ERROR: Cannot write %s\n", HOST_CSV); return; }

    fprintf(fp, "HostID,FirstName,LastName,Organization,Phone,Email\n");
    for (int i = 0; i < gHosts.count; i++) {
        Host *h = &gHosts.rows[i];
        fprintf(fp, "%d,%s,%s,%s,%s,%s\n",
                h->HostID, h->FirstName, h->LastName, h->Organization, h->Phone, h->Email);
    }
    fclose(fp);
}

static void loadHosts(void) {
    FILE *fp = fopen(HOST_CSV, "r");
    if (!fp) return;

    char line[LINE_BUF];
    fgets(line, sizeof(line), fp); /* header */

    gHosts.count = 0;

    while (fgets(line, sizeof(line), fp)) {
        trimNewline(line);
        char *cur = line;

        Host h;
        memset(&h, 0, sizeof(h));

        h.HostID = toIntSafe(csvField(&cur), -1);
        safeCopy(h.FirstName, sizeof(h.FirstName), csvField(&cur));
        safeCopy(h.LastName, sizeof(h.LastName), csvField(&cur));
        safeCopy(h.Organization, sizeof(h.Organization), csvField(&cur));
        safeCopy(h.Phone, sizeof(h.Phone), csvField(&cur));
        safeCopy(h.Email, sizeof(h.Email), csvField(&cur));

        gHosts.rows = (Host*)ensureCapacity(gHosts.rows, &gHosts.cap,
                                           gHosts.count + 1, sizeof(Host));
        gHosts.rows[gHosts.count++] = h;
    }

    fclose(fp);
}

/* ---- Visit ---- */
static void saveVisits(void) {
    FILE *fp = fopen(VISIT_CSV, "w");
    if (!fp) { printf("ERROR: Cannot write %s\n", VISIT_CSV); return; }

    fprintf(fp, "VisitID,VisitorID,HostID,Purpose,Destination,ScheduledDate,StartTime,EndTime,Status,CreatedAt,ApprovedAt\n");
    for (int i = 0; i < gVisits.count; i++) {
        Visit *v = &gVisits.rows[i];
        fprintf(fp, "%d,%d,%d,%s,%s,%s,%s,%s,%s,%s,%s\n",
                v->VisitID, v->VisitorID, v->HostID,
                v->Purpose, v->Destination, v->ScheduledDate,
                v->StartTime, v->EndTime, v->Status,
                v->CreatedAt, v->ApprovedAt);
    }
    fclose(fp);
}

static void loadVisits(void) {
    FILE *fp = fopen(VISIT_CSV, "r");
    if (!fp) return;

    char line[LINE_BUF];
    fgets(line, sizeof(line), fp); /* header */

    gVisits.count = 0;

    while (fgets(line, sizeof(line), fp)) {
        trimNewline(line);
        char *cur = line;

        Visit v;
        memset(&v, 0, sizeof(v));

        v.VisitID = toIntSafe(csvField(&cur), -1);
        v.VisitorID = toIntSafe(csvField(&cur), -1);
        v.HostID = toIntSafe(csvField(&cur), -1);
        safeCopy(v.Purpose, sizeof(v.Purpose), csvField(&cur));
        safeCopy(v.Destination, sizeof(v.Destination), csvField(&cur));
        safeCopy(v.ScheduledDate, sizeof(v.ScheduledDate), csvField(&cur));
        safeCopy(v.StartTime, sizeof(v.StartTime), csvField(&cur));
        safeCopy(v.EndTime, sizeof(v.EndTime), csvField(&cur));
        safeCopy(v.Status, sizeof(v.Status), csvField(&cur));
        safeCopy(v.CreatedAt, sizeof(v.CreatedAt), csvField(&cur));
        safeCopy(v.ApprovedAt, sizeof(v.ApprovedAt), csvField(&cur));

        gVisits.rows = (Visit*)ensureCapacity(gVisits.rows, &gVisits.cap,
                                             gVisits.count + 1, sizeof(Visit));
        gVisits.rows[gVisits.count++] = v;
    }

    fclose(fp);
}

/* ---- AccessCode ---- */
static void saveAccessCodes(void) {
    FILE *fp = fopen(ACCESSCODE_CSV, "w");
    if (!fp) { printf("ERROR: Cannot write %s\n", ACCESSCODE_CSV); return; }

    fprintf(fp, "AccessCodeID,VisitID,CodeValue,IssuedAt,ValidFrom,ValidTo,CodeStatus\n");
    for (int i = 0; i < gAccessCodes.count; i++) {
        AccessCode *a = &gAccessCodes.rows[i];
        fprintf(fp, "%d,%d,%s,%s,%s,%s,%s\n",
                a->AccessCodeID, a->VisitID, a->CodeValue,
                a->IssuedAt, a->ValidFrom, a->ValidTo, a->CodeStatus);
    }
    fclose(fp);
}

static void loadAccessCodes(void) {
    FILE *fp = fopen(ACCESSCODE_CSV, "r");
    if (!fp) return;

    char line[LINE_BUF];
    fgets(line, sizeof(line), fp); /* header */

    gAccessCodes.count = 0;

    while (fgets(line, sizeof(line), fp)) {
        trimNewline(line);
        char *cur = line;

        AccessCode a;
        memset(&a, 0, sizeof(a));

        a.AccessCodeID = toIntSafe(csvField(&cur), -1);
        a.VisitID = toIntSafe(csvField(&cur), -1);
        safeCopy(a.CodeValue, sizeof(a.CodeValue), csvField(&cur));
        safeCopy(a.IssuedAt, sizeof(a.IssuedAt), csvField(&cur));
        safeCopy(a.ValidFrom, sizeof(a.ValidFrom), csvField(&cur));
        safeCopy(a.ValidTo, sizeof(a.ValidTo), csvField(&cur));
        safeCopy(a.CodeStatus, sizeof(a.CodeStatus), csvField(&cur));

        gAccessCodes.rows = (AccessCode*)ensureCapacity(gAccessCodes.rows, &gAccessCodes.cap,
                                                       gAccessCodes.count + 1, sizeof(AccessCode));
        gAccessCodes.rows[gAccessCodes.count++] = a;
    }

    fclose(fp);
}

/* ---- Gate ---- */
static void saveGates(void) {
    FILE *fp = fopen(GATE_CSV, "w");
    if (!fp) { printf("ERROR: Cannot write %s\n", GATE_CSV); return; }

    fprintf(fp, "GateID,GateName\n");
    for (int i = 0; i < gGates.count; i++) {
        Gate *g = &gGates.rows[i];
        fprintf(fp, "%d,%s\n", g->GateID, g->GateName);
    }
    fclose(fp);
}

static void loadGates(void) {
    FILE *fp = fopen(GATE_CSV, "r");
    if (!fp) return;

    char line[LINE_BUF];
    fgets(line, sizeof(line), fp); /* header */

    gGates.count = 0;

    while (fgets(line, sizeof(line), fp)) {
        trimNewline(line);
        char *cur = line;

        Gate g;
        memset(&g, 0, sizeof(g));

        g.GateID = toIntSafe(csvField(&cur), -1);
        safeCopy(g.GateName, sizeof(g.GateName), csvField(&cur));

        gGates.rows = (Gate*)ensureCapacity(gGates.rows, &gGates.cap,
                                           gGates.count + 1, sizeof(Gate));
        gGates.rows[gGates.count++] = g;
    }

    fclose(fp);
}

/* ---- Movement ---- */
static void saveMovements(void) {
    FILE *fp = fopen(MOVEMENT_CSV, "w");
    if (!fp) { printf("ERROR: Cannot write %s\n", MOVEMENT_CSV); return; }

    fprintf(fp, "MovementID,VisitID,AccessCodeID,VehicleID,EntryGateID,ExitGateID,EntryTime,ExitTime\n");
    for (int i = 0; i < gMovements.count; i++) {
        Movement *m = &gMovements.rows[i];

        /* NULL gate IDs stored as empty */
        char entryGateStr[16] = "";
        char exitGateStr[16] = "";
        if (m->EntryGateID != -1) sprintf(entryGateStr, "%d", m->EntryGateID);
        if (m->ExitGateID != -1) sprintf(exitGateStr, "%d", m->ExitGateID);

        fprintf(fp, "%d,%d,%d,%d,%s,%s,%s,%s\n",
                m->MovementID, m->VisitID, m->AccessCodeID, m->VehicleID,
                entryGateStr, exitGateStr, m->EntryTime, m->ExitTime);
    }
    fclose(fp);
}

static void loadMovements(void) {
    FILE *fp = fopen(MOVEMENT_CSV, "r");
    if (!fp) return;

    char line[LINE_BUF];
    fgets(line, sizeof(line), fp); /* header */

    gMovements.count = 0;

    while (fgets(line, sizeof(line), fp)) {
        trimNewline(line);
        char *cur = line;

        Movement m;
        memset(&m, 0, sizeof(m));

        m.MovementID = toIntSafe(csvField(&cur), -1);
        m.VisitID = toIntSafe(csvField(&cur), -1);
        m.AccessCodeID = toIntSafe(csvField(&cur), -1);
        m.VehicleID = toIntSafe(csvField(&cur), -1);
        m.EntryGateID = toIntSafe(csvField(&cur), -1); /* empty -> -1 */
        m.ExitGateID = toIntSafe(csvField(&cur), -1);  /* empty -> -1 */
        safeCopy(m.EntryTime, sizeof(m.EntryTime), csvField(&cur)); /* empty -> NULL */
        safeCopy(m.ExitTime, sizeof(m.ExitTime), csvField(&cur));

        gMovements.rows = (Movement*)ensureCapacity(gMovements.rows, &gMovements.cap,
                                                   gMovements.count + 1, sizeof(Movement));
        gMovements.rows[gMovements.count++] = m;
    }

    fclose(fp);
}

/* ---- Incident ---- */
static void saveIncidents(void) {
    FILE *fp = fopen(INCIDENT_CSV, "w");
    if (!fp) { printf("ERROR: Cannot write %s\n", INCIDENT_CSV); return; }

    fprintf(fp, "IncidentID,VisitID,IncidentType,Description,ReportedAt,IncidentStatus\n");
    for (int i = 0; i < gIncidents.count; i++) {
        Incident *in = &gIncidents.rows[i];
        fprintf(fp, "%d,%d,%s,%s,%s,%s\n",
                in->IncidentID, in->VisitID, in->IncidentType,
                in->Description, in->ReportedAt, in->IncidentStatus);
    }
    fclose(fp);
}

static void loadIncidents(void) {
    FILE *fp = fopen(INCIDENT_CSV, "r");
    if (!fp) return;

    char line[LINE_BUF];
    fgets(line, sizeof(line), fp); /* header */

    gIncidents.count = 0;

    while (fgets(line, sizeof(line), fp)) {
        trimNewline(line);
        char *cur = line;

        Incident in;
        memset(&in, 0, sizeof(in));

        in.IncidentID = toIntSafe(csvField(&cur), -1);
        in.VisitID = toIntSafe(csvField(&cur), -1);
        safeCopy(in.IncidentType, sizeof(in.IncidentType), csvField(&cur));
        safeCopy(in.Description, sizeof(in.Description), csvField(&cur));
        safeCopy(in.ReportedAt, sizeof(in.ReportedAt), csvField(&cur));
        safeCopy(in.IncidentStatus, sizeof(in.IncidentStatus), csvField(&cur));

        gIncidents.rows = (Incident*)ensureCapacity(gIncidents.rows, &gIncidents.cap,
                                                   gIncidents.count + 1, sizeof(Incident));
        gIncidents.rows[gIncidents.count++] = in;
    }

    fclose(fp);
}

/* Load all tables (used at startup) */
static void loadAllTables(void) {
    loadVisitors();
    loadVehicles();
    loadHosts();
    loadVisits();
    loadAccessCodes();
    loadGates();
    loadMovements();
    loadIncidents();

    rebuildVisitorIndex();
    rebuildVehicleIndex();
    rebuildHostIndex();
    rebuildVisitIndex();
    rebuildAccessCodeIndex();
    rebuildGateIndex();
    rebuildMovementIndex();
    rebuildIncidentIndex();
}

/*
FIND HELPERS (PK/FK + unique checks)
*/

/* PK find using hash index */
static int findVisitorIndex(int visitorID) {
    int idx = hashSearch(visitorHash, visitorID);
    if (idx >= 0 && idx < gVisitors.count && gVisitors.rows[idx].VisitorID == visitorID) return idx;
    /* fallback linear */
    for (int i = 0; i < gVisitors.count; i++) if (gVisitors.rows[i].VisitorID == visitorID) return i;
    return -1;
}
static int findVehicleIndex(int vehicleID) {
    int idx = hashSearch(vehicleHash, vehicleID);
    if (idx >= 0 && idx < gVehicles.count && gVehicles.rows[idx].VehicleID == vehicleID) return idx;
    for (int i = 0; i < gVehicles.count; i++) if (gVehicles.rows[i].VehicleID == vehicleID) return i;
    return -1;
}
static int findHostIndex(int hostID) {
    int idx = hashSearch(hostHash, hostID);
    if (idx >= 0 && idx < gHosts.count && gHosts.rows[idx].HostID == hostID) return idx;
    for (int i = 0; i < gHosts.count; i++) if (gHosts.rows[i].HostID == hostID) return i;
    return -1;
}
static int findVisitIndex(int visitID) {
    int idx = hashSearch(visitHash, visitID);
    if (idx >= 0 && idx < gVisits.count && gVisits.rows[idx].VisitID == visitID) return idx;
    for (int i = 0; i < gVisits.count; i++) if (gVisits.rows[i].VisitID == visitID) return i;
    return -1;
}
static int findAccessCodeIndex(int accessCodeID) {
    int idx = hashSearch(accessCodeHash, accessCodeID);
    if (idx >= 0 && idx < gAccessCodes.count && gAccessCodes.rows[idx].AccessCodeID == accessCodeID) return idx;
    for (int i = 0; i < gAccessCodes.count; i++) if (gAccessCodes.rows[i].AccessCodeID == accessCodeID) return i;
    return -1;
}
static int findGateIndex(int gateID) {
    int idx = hashSearch(gateHash, gateID);
    if (idx >= 0 && idx < gGates.count && gGates.rows[idx].GateID == gateID) return idx;
    for (int i = 0; i < gGates.count; i++) if (gGates.rows[i].GateID == gateID) return i;
    return -1;
}
static int findMovementIndex(int movementID) {
    int idx = hashSearch(movementHash, movementID);
    if (idx >= 0 && idx < gMovements.count && gMovements.rows[idx].MovementID == movementID) return idx;
    for (int i = 0; i < gMovements.count; i++) if (gMovements.rows[i].MovementID == movementID) return i;
    return -1;
}
static int findIncidentIndex(int incidentID) {
    int idx = hashSearch(incidentHash, incidentID);
    if (idx >= 0 && idx < gIncidents.count && gIncidents.rows[idx].IncidentID == incidentID) return idx;
    for (int i = 0; i < gIncidents.count; i++) if (gIncidents.rows[i].IncidentID == incidentID) return i;
    return -1;
}

/* Unique checks (linear scan) */
static int isVisitorNationalIDUnique(const char *natId, int excludeVisitorID) {
    for (int i = 0; i < gVisitors.count; i++) {
        if (gVisitors.rows[i].VisitorID == excludeVisitorID) continue;
        if (strcmp(gVisitors.rows[i].NationalID, natId) == 0) return 0;
    }
    return 1;
}
static int isVehiclePlateUnique(const char *plate, int excludeVehicleID) {
    for (int i = 0; i < gVehicles.count; i++) {
        if (gVehicles.rows[i].VehicleID == excludeVehicleID) continue;
        if (strcmp(gVehicles.rows[i].PlateNumber, plate) == 0) return 0;
    }
    return 1;
}
static int isHostEmailUnique(const char *email, int excludeHostID) {
    for (int i = 0; i < gHosts.count; i++) {
        if (gHosts.rows[i].HostID == excludeHostID) continue;
        if (strcmp(gHosts.rows[i].Email, email) == 0) return 0;
    }
    return 1;
}
static int isGateNameUnique(const char *name, int excludeGateID) {
    for (int i = 0; i < gGates.count; i++) {
        if (gGates.rows[i].GateID == excludeGateID) continue;
        if (strcmp(gGates.rows[i].GateName, name) == 0) return 0;
    }
    return 1;
}
static int isAccessCodeValueUnique(const char *code, int excludeAccessCodeID) {
    for (int i = 0; i < gAccessCodes.count; i++) {
        if (gAccessCodes.rows[i].AccessCodeID == excludeAccessCodeID) continue;
        if (strcmp(gAccessCodes.rows[i].CodeValue, code) == 0) return 0;
    }
    return 1;
}

/* One access code per visit (VisitID unique in AccessCode) */
static int hasAccessCodeForVisit(int visitID, int excludeAccessCodeID) {
    for (int i = 0; i < gAccessCodes.count; i++) {
        if (gAccessCodes.rows[i].AccessCodeID == excludeAccessCodeID) continue;
        if (gAccessCodes.rows[i].VisitID == visitID) return 1;
    }
    return 0;
}

/* Movement constraints: VisitID unique, AccessCodeID unique */
static int hasMovementForVisit(int visitID, int excludeMovementID) {
    for (int i = 0; i < gMovements.count; i++) {
        if (gMovements.rows[i].MovementID == excludeMovementID) continue;
        if (gMovements.rows[i].VisitID == visitID) return 1;
    }
    return 0;
}
static int hasMovementForAccessCode(int accessCodeID, int excludeMovementID) {
    for (int i = 0; i < gMovements.count; i++) {
        if (gMovements.rows[i].MovementID == excludeMovementID) continue;
        if (gMovements.rows[i].AccessCodeID == accessCodeID) return 1;
    }
    return 0;
}

/*
Checks for DELETE (RESTRICT style)
*/
static int visitorHasVehicles(int visitorID) {
    for (int i = 0; i < gVehicles.count; i++) if (gVehicles.rows[i].VisitorID == visitorID) return 1;
    return 0;
}
static int visitorHasVisits(int visitorID) {
    for (int i = 0; i < gVisits.count; i++) if (gVisits.rows[i].VisitorID == visitorID) return 1;
    return 0;
}
static int hostHasVisits(int hostID) {
    for (int i = 0; i < gVisits.count; i++) if (gVisits.rows[i].HostID == hostID) return 1;
    return 0;
}
static int visitHasAccessCode(int visitID) {
    for (int i = 0; i < gAccessCodes.count; i++) if (gAccessCodes.rows[i].VisitID == visitID) return 1;
    return 0;
}
static int visitHasMovement(int visitID) {
    for (int i = 0; i < gMovements.count; i++) if (gMovements.rows[i].VisitID == visitID) return 1;
    return 0;
}
static int visitHasIncidents(int visitID) {
    for (int i = 0; i < gIncidents.count; i++) if (gIncidents.rows[i].VisitID == visitID) return 1;
    return 0;
}
static int accessCodeUsedInMovement(int accessCodeID) {
    for (int i = 0; i < gMovements.count; i++) if (gMovements.rows[i].AccessCodeID == accessCodeID) return 1;
    return 0;
}
static int vehicleUsedInMovement(int vehicleID) {
    for (int i = 0; i < gMovements.count; i++) if (gMovements.rows[i].VehicleID == vehicleID) return 1;
    return 0;
}
static int gateUsedInMovement(int gateID) {
    for (int i = 0; i < gMovements.count; i++) {
        if (gMovements.rows[i].EntryGateID == gateID) return 1;
        if (gMovements.rows[i].ExitGateID == gateID) return 1;
    }
    return 0;
}

/*
CRUD for all tables
*/

/* ---------- VISITOR CRUD ---------- */

/* SQL: INSERT INTO Visitor(VisitorID,NationalID,FirstName,LastName,Phone,Email) VALUES (?,?,?,?,?,?); */
int insertVisitor(Visitor v) {
    if (findVisitorIndex(v.VisitorID) != -1) {
        printf("INSERT Visitor failed: duplicate VisitorID.\n");
        return 0;
    }
    if (!isVisitorNationalIDUnique(v.NationalID, -1)) {
        printf("INSERT Visitor failed: duplicate NationalID.\n");
        return 0;
    }

    gVisitors.rows = (Visitor*)ensureCapacity(gVisitors.rows, &gVisitors.cap,
                                             gVisitors.count + 1, sizeof(Visitor));
    gVisitors.rows[gVisitors.count++] = v;

    rebuildVisitorIndex();
    saveVisitors();

    writeLog("Visitor", "VisitorID", v.VisitorID, "NULL", "NEW", "INSERT");
    writeLog("Visitor", "NationalID", v.VisitorID, "NULL", v.NationalID, "INSERT");
    writeLog("Visitor", "FirstName", v.VisitorID, "NULL", v.FirstName, "INSERT");
    writeLog("Visitor", "LastName", v.VisitorID, "NULL", v.LastName, "INSERT");
    writeLog("Visitor", "Phone", v.VisitorID, "NULL", v.Phone, "INSERT");
    writeLog("Visitor", "Email", v.VisitorID, "NULL", v.Email, "INSERT");

    return 1;
}

/* SQL: SELECT * FROM Visitor; */
void selectAllVisitors(void) {
    printf("---- Visitor (%d rows) ----\n", gVisitors.count);
    for (int i = 0; i < gVisitors.count; i++) {
        Visitor *v = &gVisitors.rows[i];
        printf("%d | %s | %s %s | %s | %s\n",
               v->VisitorID, v->NationalID, v->FirstName, v->LastName, v->Phone, v->Email);
    }
}

/* SQL: UPDATE Visitor SET NationalID=?, FirstName=?, LastName=?, Phone=?, Email=? WHERE VisitorID=?; */
int updateVisitor(Visitor v) {
    int idx = findVisitorIndex(v.VisitorID);
    if (idx == -1) {
        printf("UPDATE Visitor failed: VisitorID not found.\n");
        return 0;
    }
    if (!isVisitorNationalIDUnique(v.NationalID, v.VisitorID)) {
        printf("UPDATE Visitor failed: duplicate NationalID.\n");
        return 0;
    }

    Visitor old = gVisitors.rows[idx];
    gVisitors.rows[idx] = v;

    saveVisitors();

    writeLog("Visitor", "NationalID", v.VisitorID, old.NationalID, v.NationalID, "UPDATE");
    writeLog("Visitor", "FirstName", v.VisitorID, old.FirstName, v.FirstName, "UPDATE");
    writeLog("Visitor", "LastName", v.VisitorID, old.LastName, v.LastName, "UPDATE");
    writeLog("Visitor", "Phone", v.VisitorID, old.Phone, v.Phone, "UPDATE");
    writeLog("Visitor", "Email", v.VisitorID, old.Email, v.Email, "UPDATE");

    return 1;
}

/* SQL: DELETE FROM Visitor WHERE VisitorID=?; (RESTRICT if referenced) */
int deleteVisitor(int visitorID) {
    int idx = findVisitorIndex(visitorID);
    if (idx == -1) {
        printf("DELETE Visitor failed: VisitorID not found.\n");
        return 0;
    }
    if (visitorHasVehicles(visitorID) || visitorHasVisits(visitorID)) {
        printf("DELETE Visitor failed: referenced by Vehicle/Visit (RESTRICT).\n");
        return 0;
    }

    writeLog("Visitor", "VisitorID", visitorID, "OLD", "NULL", "DELETE");

    /* remove row by shifting */
    for (int i = idx; i < gVisitors.count - 1; i++) {
        gVisitors.rows[i] = gVisitors.rows[i + 1];
    }
    gVisitors.count--;

    rebuildVisitorIndex();
    saveVisitors();

    return 1;
}

/* ---------- VEHICLE CRUD ---------- */

/* SQL: INSERT INTO Vehicle(VehicleID,VisitorID,PlateNumber,Type,Color,Manufacturer,Model) VALUES (?,?,?,?,?,?,?); */
int insertVehicle(Vehicle v) {
    if (findVehicleIndex(v.VehicleID) != -1) {
        printf("INSERT Vehicle failed: duplicate VehicleID.\n");
        return 0;
    }
    if (findVisitorIndex(v.VisitorID) == -1) {
        printf("INSERT Vehicle failed: invalid VisitorID (FK).\n");
        return 0;
    }
    if (!isVehiclePlateUnique(v.PlateNumber, -1)) {
        printf("INSERT Vehicle failed: duplicate PlateNumber.\n");
        return 0;
    }

    gVehicles.rows = (Vehicle*)ensureCapacity(gVehicles.rows, &gVehicles.cap,
                                             gVehicles.count + 1, sizeof(Vehicle));
    gVehicles.rows[gVehicles.count++] = v;

    rebuildVehicleIndex();
    saveVehicles();

    writeLog("Vehicle", "VehicleID", v.VehicleID, "NULL", "NEW", "INSERT");
    writeLog("Vehicle", "VisitorID", v.VehicleID, "NULL", "FK", "INSERT");
    writeLog("Vehicle", "PlateNumber", v.VehicleID, "NULL", v.PlateNumber, "INSERT");

    return 1;
}

/* SQL: SELECT * FROM Vehicle; */
void selectAllVehicles(void) {
    printf("---- Vehicle (%d rows) ----\n", gVehicles.count);
    for (int i = 0; i < gVehicles.count; i++) {
        Vehicle *v = &gVehicles.rows[i];
        printf("%d | Visitor:%d | %s | %s | %s | %s %s\n",
               v->VehicleID, v->VisitorID, v->PlateNumber, v->Type, v->Color, v->Manufacturer, v->Model);
    }
}

/* SQL: UPDATE Vehicle SET VisitorID=?, PlateNumber=?, Type=?, Color=?, Manufacturer=?, Model=? WHERE VehicleID=?; */
int updateVehicle(Vehicle v) {
    int idx = findVehicleIndex(v.VehicleID);
    if (idx == -1) {
        printf("UPDATE Vehicle failed: VehicleID not found.\n");
        return 0;
    }
    if (findVisitorIndex(v.VisitorID) == -1) {
        printf("UPDATE Vehicle failed: invalid VisitorID (FK).\n");
        return 0;
    }
    if (!isVehiclePlateUnique(v.PlateNumber, v.VehicleID)) {
        printf("UPDATE Vehicle failed: duplicate PlateNumber.\n");
        return 0;
    }

    Vehicle old = gVehicles.rows[idx];
    gVehicles.rows[idx] = v;

    saveVehicles();

    writeLog("Vehicle", "VisitorID", v.VehicleID, "OLD", "NEW", "UPDATE");
    writeLog("Vehicle", "PlateNumber", v.VehicleID, old.PlateNumber, v.PlateNumber, "UPDATE");
    return 1;
}

/* SQL: DELETE FROM Vehicle WHERE VehicleID=?; (RESTRICT if referenced) */
int deleteVehicle(int vehicleID) {
    int idx = findVehicleIndex(vehicleID);
    if (idx == -1) {
        printf("DELETE Vehicle failed: VehicleID not found.\n");
        return 0;
    }
    if (vehicleUsedInMovement(vehicleID)) {
        printf("DELETE Vehicle failed: referenced by Movement (RESTRICT).\n");
        return 0;
    }

    writeLog("Vehicle", "VehicleID", vehicleID, "OLD", "NULL", "DELETE");

    for (int i = idx; i < gVehicles.count - 1; i++) {
        gVehicles.rows[i] = gVehicles.rows[i + 1];
    }
    gVehicles.count--;

    rebuildVehicleIndex();
    saveVehicles();
    return 1;
}

/* ---------- HOST CRUD ---------- */

/* SQL: INSERT INTO Host(HostID,FirstName,LastName,Organization,Phone,Email) VALUES (?,?,?,?,?,?); */
int insertHost(Host h) {
    if (findHostIndex(h.HostID) != -1) {
        printf("INSERT Host failed: duplicate HostID.\n");
        return 0;
    }
    if (!isHostEmailUnique(h.Email, -1)) {
        printf("INSERT Host failed: duplicate Email.\n");
        return 0;
    }

    gHosts.rows = (Host*)ensureCapacity(gHosts.rows, &gHosts.cap,
                                       gHosts.count + 1, sizeof(Host));
    gHosts.rows[gHosts.count++] = h;

    rebuildHostIndex();
    saveHosts();

    writeLog("Host", "HostID", h.HostID, "NULL", "NEW", "INSERT");
    writeLog("Host", "Email", h.HostID, "NULL", h.Email, "INSERT");

    return 1;
}

/* SQL: SELECT * FROM Host; */
void selectAllHosts(void) {
    printf("---- Host (%d rows) ----\n", gHosts.count);
    for (int i = 0; i < gHosts.count; i++) {
        Host *h = &gHosts.rows[i];
        printf("%d | %s %s | %s | %s | %s\n",
               h->HostID, h->FirstName, h->LastName, h->Organization, h->Phone, h->Email);
    }
}

/* SQL: UPDATE Host SET FirstName=?,LastName=?,Organization=?,Phone=?,Email=? WHERE HostID=?; */
int updateHost(Host h) {
    int idx = findHostIndex(h.HostID);
    if (idx == -1) {
        printf("UPDATE Host failed: HostID not found.\n");
        return 0;
    }
    if (!isHostEmailUnique(h.Email, h.HostID)) {
        printf("UPDATE Host failed: duplicate Email.\n");
        return 0;
    }

    Host old = gHosts.rows[idx];
    gHosts.rows[idx] = h;

    saveHosts();

    writeLog("Host", "Email", h.HostID, old.Email, h.Email, "UPDATE");
    return 1;
}

/* SQL: DELETE FROM Host WHERE HostID=?; (RESTRICT if referenced) */
int deleteHost(int hostID) {
    int idx = findHostIndex(hostID);
    if (idx == -1) {
        printf("DELETE Host failed: HostID not found.\n");
        return 0;
    }
    if (hostHasVisits(hostID)) {
        printf("DELETE Host failed: referenced by Visit (RESTRICT).\n");
        return 0;
    }

    writeLog("Host", "HostID", hostID, "OLD", "NULL", "DELETE");

    for (int i = idx; i < gHosts.count - 1; i++) {
        gHosts.rows[i] = gHosts.rows[i + 1];
    }
    gHosts.count--;

    rebuildHostIndex();
    saveHosts();
    return 1;
}

/* ---------- VISIT CRUD ---------- */

/* SQL: INSERT INTO Visit(...) VALUES (...); */
int insertVisit(Visit v) {
    if (findVisitIndex(v.VisitID) != -1) {
        printf("INSERT Visit failed: duplicate VisitID.\n");
        return 0;
    }
    if (findVisitorIndex(v.VisitorID) == -1) {
        printf("INSERT Visit failed: invalid VisitorID (FK).\n");
        return 0;
    }
    if (findHostIndex(v.HostID) == -1) {
        printf("INSERT Visit failed: invalid HostID (FK).\n");
        return 0;
    }

    /* Recommended check: EndTime > StartTime */
    int st = timeToSeconds(v.StartTime);
    int et = timeToSeconds(v.EndTime);
    if (st < 0 || et < 0 || et <= st) {
        printf("INSERT Visit failed: EndTime must be > StartTime.\n");
        return 0;
    }

    /* If Status is Approved, ApprovedAt should not be empty */
    if (strcmp(v.Status, "Approved") == 0 && v.ApprovedAt[0] == '\0') {
        printf("INSERT Visit failed: ApprovedAt must be set when Status='Approved'.\n");
        return 0;
    }

    gVisits.rows = (Visit*)ensureCapacity(gVisits.rows, &gVisits.cap,
                                         gVisits.count + 1, sizeof(Visit));
    gVisits.rows[gVisits.count++] = v;

    rebuildVisitIndex();
    saveVisits();

    writeLog("Visit", "VisitID", v.VisitID, "NULL", "NEW", "INSERT");
    writeLog("Visit", "VisitorID", v.VisitID, "NULL", "FK", "INSERT");
    writeLog("Visit", "HostID", v.VisitID, "NULL", "FK", "INSERT");
    writeLog("Visit", "Status", v.VisitID, "NULL", v.Status, "INSERT");

    return 1;
}

/* SQL: SELECT * FROM Visit; */
void selectAllVisits(void) {
    printf("---- Visit (%d rows) ----\n", gVisits.count);
    for (int i = 0; i < gVisits.count; i++) {
        Visit *v = &gVisits.rows[i];
        printf("%d | Visitor:%d Host:%d | %s | %s | %s %s-%s | %s\n",
               v->VisitID, v->VisitorID, v->HostID,
               v->Purpose, v->Destination,
               v->ScheduledDate, v->StartTime, v->EndTime,
               v->Status);
    }
}

/* SQL: UPDATE Visit SET ... WHERE VisitID=?; */
int updateVisit(Visit v) {
    int idx = findVisitIndex(v.VisitID);
    if (idx == -1) {
        printf("UPDATE Visit failed: VisitID not found.\n");
        return 0;
    }
    if (findVisitorIndex(v.VisitorID) == -1) {
        printf("UPDATE Visit failed: invalid VisitorID (FK).\n");
        return 0;
    }
    if (findHostIndex(v.HostID) == -1) {
        printf("UPDATE Visit failed: invalid HostID (FK).\n");
        return 0;
    }

    int st = timeToSeconds(v.StartTime);
    int et = timeToSeconds(v.EndTime);
    if (st < 0 || et < 0 || et <= st) {
        printf("UPDATE Visit failed: EndTime must be > StartTime.\n");
        return 0;
    }

    if (strcmp(v.Status, "Approved") == 0 && v.ApprovedAt[0] == '\0') {
        printf("UPDATE Visit failed: ApprovedAt must be set when Status='Approved'.\n");
        return 0;
    }

    Visit old = gVisits.rows[idx];
    gVisits.rows[idx] = v;

    saveVisits();

    writeLog("Visit", "Status", v.VisitID, old.Status, v.Status, "UPDATE");
    writeLog("Visit", "ApprovedAt", v.VisitID, old.ApprovedAt, v.ApprovedAt, "UPDATE");

    return 1;
}

/* SQL: DELETE FROM Visit WHERE VisitID=?; (RESTRICT if referenced) */
int deleteVisit(int visitID) {
    int idx = findVisitIndex(visitID);
    if (idx == -1) {
        printf("DELETE Visit failed: VisitID not found.\n");
        return 0;
    }
    if (visitHasAccessCode(visitID) || visitHasMovement(visitID) || visitHasIncidents(visitID)) {
        printf("DELETE Visit failed: referenced by AccessCode/Movement/Incident (RESTRICT).\n");
        return 0;
    }

    writeLog("Visit", "VisitID", visitID, "OLD", "NULL", "DELETE");

    for (int i = idx; i < gVisits.count - 1; i++) {
        gVisits.rows[i] = gVisits.rows[i + 1];
    }
    gVisits.count--;

    rebuildVisitIndex();
    saveVisits();
    return 1;
}

/* ---------- ACCESSCODE CRUD ---------- */

/* SQL: INSERT INTO AccessCode(AccessCodeID,VisitID,CodeValue,IssuedAt,ValidFrom,ValidTo,CodeStatus) VALUES (?,?,?,?,?,?,?); */
int insertAccessCode(AccessCode a) {
    if (findAccessCodeIndex(a.AccessCodeID) != -1) {
        printf("INSERT AccessCode failed: duplicate AccessCodeID.\n");
        return 0;
    }
    if (findVisitIndex(a.VisitID) == -1) {
        printf("INSERT AccessCode failed: invalid VisitID (FK).\n");
        return 0;
    }
    if (hasAccessCodeForVisit(a.VisitID, -1)) {
        printf("INSERT AccessCode failed: VisitID already has an AccessCode (UNIQUE).\n");
        return 0;
    }
    if (!isAccessCodeValueUnique(a.CodeValue, -1)) {
        printf("INSERT AccessCode failed: duplicate CodeValue.\n");
        return 0;
    }
    /* check: ValidTo > ValidFrom */
    if (strcmp(a.ValidTo, a.ValidFrom) <= 0) {
        printf("INSERT AccessCode failed: ValidTo must be > ValidFrom.\n");
        return 0;
    }

    gAccessCodes.rows = (AccessCode*)ensureCapacity(gAccessCodes.rows, &gAccessCodes.cap,
                                                   gAccessCodes.count + 1, sizeof(AccessCode));
    gAccessCodes.rows[gAccessCodes.count++] = a;

    rebuildAccessCodeIndex();
    saveAccessCodes();

    writeLog("AccessCode", "AccessCodeID", a.AccessCodeID, "NULL", "NEW", "INSERT");
    writeLog("AccessCode", "VisitID", a.AccessCodeID, "NULL", "FK/UNIQUE", "INSERT");
    writeLog("AccessCode", "CodeValue", a.AccessCodeID, "NULL", a.CodeValue, "INSERT");

    return 1;
}

/* SQL: SELECT * FROM AccessCode; */
void selectAllAccessCodes(void) {
    printf("---- AccessCode (%d rows) ----\n", gAccessCodes.count);
    for (int i = 0; i < gAccessCodes.count; i++) {
        AccessCode *a = &gAccessCodes.rows[i];
        printf("%d | Visit:%d | %s | %s -> %s | %s\n",
               a->AccessCodeID, a->VisitID, a->CodeValue, a->ValidFrom, a->ValidTo, a->CodeStatus);
    }
}

/* SQL: UPDATE AccessCode SET VisitID=?,CodeValue=?,IssuedAt=?,ValidFrom=?,ValidTo=?,CodeStatus=? WHERE AccessCodeID=?; */
int updateAccessCode(AccessCode a) {
    int idx = findAccessCodeIndex(a.AccessCodeID);
    if (idx == -1) {
        printf("UPDATE AccessCode failed: AccessCodeID not found.\n");
        return 0;
    }
    if (findVisitIndex(a.VisitID) == -1) {
        printf("UPDATE AccessCode failed: invalid VisitID (FK).\n");
        return 0;
    }
    if (hasAccessCodeForVisit(a.VisitID, a.AccessCodeID)) {
        printf("UPDATE AccessCode failed: VisitID already has another AccessCode.\n");
        return 0;
    }
    if (!isAccessCodeValueUnique(a.CodeValue, a.AccessCodeID)) {
        printf("UPDATE AccessCode failed: duplicate CodeValue.\n");
        return 0;
    }
    if (strcmp(a.ValidTo, a.ValidFrom) <= 0) {
        printf("UPDATE AccessCode failed: ValidTo must be > ValidFrom.\n");
        return 0;
    }
    if (accessCodeUsedInMovement(a.AccessCodeID)) {
        AccessCode oldUsed = gAccessCodes.rows[idx];
        if (a.VisitID != oldUsed.VisitID) {
            printf("UPDATE AccessCode failed: cannot change VisitID when used in Movement.\n");
            return 0;
        }
    }

    AccessCode old = gAccessCodes.rows[idx];
    gAccessCodes.rows[idx] = a;

    saveAccessCodes();

    writeLog("AccessCode", "CodeValue", a.AccessCodeID, old.CodeValue, a.CodeValue, "UPDATE");
    writeLog("AccessCode", "CodeStatus", a.AccessCodeID, old.CodeStatus, a.CodeStatus, "UPDATE");
    return 1;
}

/* SQL: DELETE FROM AccessCode WHERE AccessCodeID=?; (RESTRICT if referenced) */
int deleteAccessCode(int accessCodeID) {
    int idx = findAccessCodeIndex(accessCodeID);
    if (idx == -1) {
        printf("DELETE AccessCode failed: AccessCodeID not found.\n");
        return 0;
    }
    if (accessCodeUsedInMovement(accessCodeID)) {
        printf("DELETE AccessCode failed: referenced by Movement (RESTRICT).\n");
        return 0;
    }

    writeLog("AccessCode", "AccessCodeID", accessCodeID, "OLD", "NULL", "DELETE");

    for (int i = idx; i < gAccessCodes.count - 1; i++) {
        gAccessCodes.rows[i] = gAccessCodes.rows[i + 1];
    }
    gAccessCodes.count--;

    rebuildAccessCodeIndex();
    saveAccessCodes();
    return 1;
}

/* ---------- GATE CRUD ---------- */

/* SQL: INSERT INTO Gate(GateID,GateName) VALUES (?,?); */
int insertGate(Gate g) {
    if (findGateIndex(g.GateID) != -1) {
        printf("INSERT Gate failed: duplicate GateID.\n");
        return 0;
    }
    if (!isGateNameUnique(g.GateName, -1)) {
        printf("INSERT Gate failed: duplicate GateName.\n");
        return 0;
    }

    gGates.rows = (Gate*)ensureCapacity(gGates.rows, &gGates.cap,
                                       gGates.count + 1, sizeof(Gate));
    gGates.rows[gGates.count++] = g;

    rebuildGateIndex();
    saveGates();

    writeLog("Gate", "GateID", g.GateID, "NULL", "NEW", "INSERT");
    writeLog("Gate", "GateName", g.GateID, "NULL", g.GateName, "INSERT");
    return 1;
}

/* SQL: SELECT * FROM Gate; */
void selectAllGates(void) {
    printf("---- Gate (%d rows) ----\n", gGates.count);
    for (int i = 0; i < gGates.count; i++) {
        Gate *g = &gGates.rows[i];
        printf("%d | %s\n", g->GateID, g->GateName);
    }
}

/* SQL: UPDATE Gate SET GateName=? WHERE GateID=?; */
int updateGate(Gate g) {
    int idx = findGateIndex(g.GateID);
    if (idx == -1) {
        printf("UPDATE Gate failed: GateID not found.\n");
        return 0;
    }
    if (!isGateNameUnique(g.GateName, g.GateID)) {
        printf("UPDATE Gate failed: duplicate GateName.\n");
        return 0;
    }

    Gate old = gGates.rows[idx];
    gGates.rows[idx] = g;

    saveGates();

    writeLog("Gate", "GateName", g.GateID, old.GateName, g.GateName, "UPDATE");
    return 1;
}

/* SQL: DELETE FROM Gate WHERE GateID=?; (RESTRICT if referenced) */
int deleteGate(int gateID) {
    int idx = findGateIndex(gateID);
    if (idx == -1) {
        printf("DELETE Gate failed: GateID not found.\n");
        return 0;
    }
    if (gateUsedInMovement(gateID)) {
        printf("DELETE Gate failed: referenced by Movement (RESTRICT).\n");
        return 0;
    }

    writeLog("Gate", "GateID", gateID, "OLD", "NULL", "DELETE");

    for (int i = idx; i < gGates.count - 1; i++) {
        gGates.rows[i] = gGates.rows[i + 1];
    }
    gGates.count--;

    rebuildGateIndex();
    saveGates();
    return 1;
}

/* ---------- MOVEMENT CRUD ---------- */

/* SQL: INSERT INTO Movement(...) VALUES (...); */
int insertMovement(Movement m) {
    if (findMovementIndex(m.MovementID) != -1) {
        printf("INSERT Movement failed: duplicate MovementID.\n");
        return 0;
    }
    int vIdx = findVisitIndex(m.VisitID);
    if (vIdx == -1) {
        printf("INSERT Movement failed: invalid VisitID (FK).\n");
        return 0;
    }
    if (strcmp(gVisits.rows[vIdx].Status, "Approved") != 0) {
        printf("INSERT Movement failed: Visit must be Approved (business rule).\n");
        return 0;
    }
    if (findAccessCodeIndex(m.AccessCodeID) == -1) {
        printf("INSERT Movement failed: invalid AccessCodeID (FK).\n");
        return 0;
    }
    if (findVehicleIndex(m.VehicleID) == -1) {
        printf("INSERT Movement failed: invalid VehicleID (FK).\n");
        return 0;
    }

    if (m.EntryGateID != -1 && findGateIndex(m.EntryGateID) == -1) {
        printf("INSERT Movement failed: invalid EntryGateID (FK).\n");
        return 0;
    }
    if (m.ExitGateID != -1 && findGateIndex(m.ExitGateID) == -1) {
        printf("INSERT Movement failed: invalid ExitGateID (FK).\n");
        return 0;
    }

    if (hasMovementForVisit(m.VisitID, -1)) {
        printf("INSERT Movement failed: VisitID already has a Movement (UNIQUE).\n");
        return 0;
    }
    if (hasMovementForAccessCode(m.AccessCodeID, -1)) {
        printf("INSERT Movement failed: AccessCodeID already used in Movement (UNIQUE).\n");
        return 0;
    }

    /* check: ExitTime NULL or >= EntryTime */
    if (m.EntryTime[0] != '\0' && m.ExitTime[0] != '\0') {
        if (strcmp(m.ExitTime, m.EntryTime) < 0) {
            printf("INSERT Movement failed: ExitTime must be >= EntryTime.\n");
            return 0;
        }
    }

    gMovements.rows = (Movement*)ensureCapacity(gMovements.rows, &gMovements.cap,
                                               gMovements.count + 1, sizeof(Movement));
    gMovements.rows[gMovements.count++] = m;

    rebuildMovementIndex();
    saveMovements();

    writeLog("Movement", "MovementID", m.MovementID, "NULL", "NEW", "INSERT");
    writeLog("Movement", "VisitID", m.MovementID, "NULL", "FK/UNIQUE", "INSERT");
    writeLog("Movement", "AccessCodeID", m.MovementID, "NULL", "FK/UNIQUE", "INSERT");
    return 1;
}

/* SQL: SELECT * FROM Movement; */
void selectAllMovements(void) {
    printf("---- Movement (%d rows) ----\n", gMovements.count);
    for (int i = 0; i < gMovements.count; i++) {
        Movement *m = &gMovements.rows[i];
        printf("%d | Visit:%d | Code:%d | Veh:%d | EntryGate:%d ExitGate:%d | Entry:%s Exit:%s\n",
               m->MovementID, m->VisitID, m->AccessCodeID, m->VehicleID,
               m->EntryGateID, m->ExitGateID, m->EntryTime, m->ExitTime);
    }
}

/* SQL: UPDATE Movement SET EntryGateID=?,ExitGateID=?,EntryTime=?,ExitTime=? WHERE MovementID=?; */
int updateMovement(Movement m) {
    int idx = findMovementIndex(m.MovementID);
    if (idx == -1) {
        printf("UPDATE Movement failed: MovementID not found.\n");
        return 0;
    }

    /* FK checks */
    if (findVisitIndex(m.VisitID) == -1) { printf("UPDATE Movement failed: invalid VisitID.\n"); return 0; }
    if (findAccessCodeIndex(m.AccessCodeID) == -1) { printf("UPDATE Movement failed: invalid AccessCodeID.\n"); return 0; }
    if (findVehicleIndex(m.VehicleID) == -1) { printf("UPDATE Movement failed: invalid VehicleID.\n"); return 0; }
    if (m.EntryGateID != -1 && findGateIndex(m.EntryGateID) == -1) { printf("UPDATE Movement failed: invalid EntryGateID.\n"); return 0; }
    if (m.ExitGateID != -1 && findGateIndex(m.ExitGateID) == -1) { printf("UPDATE Movement failed: invalid ExitGateID.\n"); return 0; }

    if (hasMovementForVisit(m.VisitID, m.MovementID)) { printf("UPDATE Movement failed: VisitID unique violation.\n"); return 0; }
    if (hasMovementForAccessCode(m.AccessCodeID, m.MovementID)) { printf("UPDATE Movement failed: AccessCodeID unique violation.\n"); return 0; }

    if (m.EntryTime[0] != '\0' && m.ExitTime[0] != '\0') {
        if (strcmp(m.ExitTime, m.EntryTime) < 0) {
            printf("UPDATE Movement failed: ExitTime must be >= EntryTime.\n");
            return 0;
        }
    }

    Movement old = gMovements.rows[idx];
    gMovements.rows[idx] = m;

    saveMovements();

    writeLog("Movement", "EntryTime", m.MovementID, old.EntryTime, m.EntryTime, "UPDATE");
    writeLog("Movement", "ExitTime", m.MovementID, old.ExitTime, m.ExitTime, "UPDATE");
    return 1;
}

/* SQL: DELETE FROM Movement WHERE MovementID=?; */
int deleteMovement(int movementID) {
    int idx = findMovementIndex(movementID);
    if (idx == -1) {
        printf("DELETE Movement failed: MovementID not found.\n");
        return 0;
    }

    writeLog("Movement", "MovementID", movementID, "OLD", "NULL", "DELETE");

    for (int i = idx; i < gMovements.count - 1; i++) {
        gMovements.rows[i] = gMovements.rows[i + 1];
    }
    gMovements.count--;

    rebuildMovementIndex();
    saveMovements();
    return 1;
}

/* ---------- INCIDENT CRUD ---------- */

/* SQL: INSERT INTO Incident(IncidentID,VisitID,IncidentType,Description,ReportedAt,IncidentStatus) VALUES (?,?,?,?,?,?); */
int insertIncident(Incident in) {
    if (findIncidentIndex(in.IncidentID) != -1) {
        printf("INSERT Incident failed: duplicate IncidentID.\n");
        return 0;
    }
    if (findVisitIndex(in.VisitID) == -1) {
        printf("INSERT Incident failed: invalid VisitID (FK).\n");
        return 0;
    }

    gIncidents.rows = (Incident*)ensureCapacity(gIncidents.rows, &gIncidents.cap,
                                               gIncidents.count + 1, sizeof(Incident));
    gIncidents.rows[gIncidents.count++] = in;

    rebuildIncidentIndex();
    saveIncidents();

    writeLog("Incident", "IncidentID", in.IncidentID, "NULL", "NEW", "INSERT");
    writeLog("Incident", "VisitID", in.IncidentID, "NULL", "FK", "INSERT");
    writeLog("Incident", "IncidentType", in.IncidentID, "NULL", in.IncidentType, "INSERT");

    return 1;
}

/* SQL: SELECT * FROM Incident; */
void selectAllIncidents(void) {
    printf("---- Incident (%d rows) ----\n", gIncidents.count);
    for (int i = 0; i < gIncidents.count; i++) {
        Incident *in = &gIncidents.rows[i];
        printf("%d | Visit:%d | %s | %s | %s | %s\n",
               in->IncidentID, in->VisitID, in->IncidentType, in->Description, in->ReportedAt, in->IncidentStatus);
    }
}

/* SQL: UPDATE Incident SET VisitID=?,IncidentType=?,Description=?,ReportedAt=?,IncidentStatus=? WHERE IncidentID=?; */
int updateIncident(Incident in) {
    int idx = findIncidentIndex(in.IncidentID);
    if (idx == -1) {
        printf("UPDATE Incident failed: IncidentID not found.\n");
        return 0;
    }
    if (findVisitIndex(in.VisitID) == -1) {
        printf("UPDATE Incident failed: invalid VisitID (FK).\n");
        return 0;
    }

    Incident old = gIncidents.rows[idx];
    gIncidents.rows[idx] = in;

    saveIncidents();

    writeLog("Incident", "IncidentStatus", in.IncidentID, old.IncidentStatus, in.IncidentStatus, "UPDATE");
    return 1;
}

/* SQL: DELETE FROM Incident WHERE IncidentID=?; */
int deleteIncident(int incidentID) {
    int idx = findIncidentIndex(incidentID);
    if (idx == -1) {
        printf("DELETE Incident failed: IncidentID not found.\n");
        return 0;
    }

    writeLog("Incident", "IncidentID", incidentID, "OLD", "NULL", "DELETE");

    for (int i = idx; i < gIncidents.count - 1; i++) {
        gIncidents.rows[i] = gIncidents.rows[i + 1];
    }
    gIncidents.count--;

    rebuildIncidentIndex();
    saveIncidents();
    return 1;
}

/*
Concurrency Control (Locking-based)
- Shared (S) lock for READ
- Exclusive (X) lock for CUD
- Waiting queue per table
*/

#define MAX_WAIT 100

typedef enum {
    T_VISITOR = 0,
    T_VEHICLE,
    T_HOST,
    T_VISIT,
    T_ACCESSCODE,
    T_GATE,
    T_MOVEMENT,
    T_INCIDENT,
    T_TABLE_COUNT
} TableId;

typedef struct {
    int userId;
    char lockType; /* 'S' or 'X' */
} LockRequest;

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t  cond;

    int sharedCount;     /* number of shared holders */
    int exclusiveUser;   /* -1 if none, else userId holding X */

    LockRequest waitQ[MAX_WAIT];
    int qCount;
} LockManager;

/* The "Lock Table" (one lock manager per table) */
static LockManager gLockTable[T_TABLE_COUNT];

static void lockTableInitOne(LockManager *lm) {
    pthread_mutex_init(&lm->mutex, NULL);
    pthread_cond_init(&lm->cond, NULL);
    lm->sharedCount = 0;
    lm->exclusiveUser = -1;
    lm->qCount = 0;
}

static void initLockTable(void) {
    for (int i = 0; i < T_TABLE_COUNT; i++) {
        lockTableInitOne(&gLockTable[i]);
    }
}

/* Queue helpers */
static void enqueueReq(LockManager *lm, int userId, char lockType) {
    if (lm->qCount >= MAX_WAIT) {
        printf("ERROR: Waiting queue full!\n");
        return;
    }
    lm->waitQ[lm->qCount].userId = userId;
    lm->waitQ[lm->qCount].lockType = lockType;
    lm->qCount++;
}

static int isHeadReq(LockManager *lm, int userId, char lockType) {
    if (lm->qCount <= 0) return 0;
    return (lm->waitQ[0].userId == userId && lm->waitQ[0].lockType == lockType);
}

static void dequeueHead(LockManager *lm) {
    if (lm->qCount <= 0) return;
    for (int i = 0; i < lm->qCount - 1; i++) {
        lm->waitQ[i] = lm->waitQ[i + 1];
    }
    lm->qCount--;
}

/*
Acquire Shared Lock (S) for READ
- If no X lock exists, start immediately (even if writers are waiting).
- If X lock exists, wait in queue until it's your turn and no X lock.
*/
static void acquireSLock(TableId t, int userId, const char *opText) {
    LockManager *lm = &gLockTable[t];

    pthread_mutex_lock(&lm->mutex);

    /* If no exclusive lock exists, READ can start immediately */
    if (lm->exclusiveUser == -1) {
        lm->sharedCount++;
        pthread_mutex_unlock(&lm->mutex);
        printf("User %d: %s. Started.\n", userId, opText);
        return;
    }

    /* Otherwise wait */
    enqueueReq(lm, userId, 'S');
    printf("User %d: %s. Wait.\n", userId, opText);

    while (1) {
        pthread_cond_wait(&lm->cond, &lm->mutex);

        /* Allowed only when no X lock, and you are first in waiting queue */
        if (lm->exclusiveUser == -1 && isHeadReq(lm, userId, 'S')) {
            dequeueHead(lm);
            lm->sharedCount++;
            pthread_mutex_unlock(&lm->mutex);
            printf("User %d: %s. Started.\n", userId, opText);
            return;
        }
    }
}

/*
Acquire Exclusive Lock (X) for C/U/D
- Must wait if ANY lock exists (S or X).
- Can start only when no locks and you are first in queue.
*/
static void acquireXLock(TableId t, int userId, const char *opText) {
    LockManager *lm = &gLockTable[t];

    pthread_mutex_lock(&lm->mutex);

    if (lm->exclusiveUser == -1 && lm->sharedCount == 0) {
        lm->exclusiveUser = userId;
        pthread_mutex_unlock(&lm->mutex);
        printf("User %d: %s. Started.\n", userId, opText);
        return;
    }

    enqueueReq(lm, userId, 'X');
    printf("User %d: %s. Wait.\n", userId, opText);

    while (1) {
        pthread_cond_wait(&lm->cond, &lm->mutex);

        if (lm->exclusiveUser == -1 && lm->sharedCount == 0 && isHeadReq(lm, userId, 'X')) {
            dequeueHead(lm);
            lm->exclusiveUser = userId;
            pthread_mutex_unlock(&lm->mutex);
            printf("User %d: %s. Started.\n", userId, opText);
            return;
        }
    }
}

/* Release Shared Lock */
static void releaseSLock(TableId t, int userId) {
    LockManager *lm = &gLockTable[t];

    pthread_mutex_lock(&lm->mutex);

    if (lm->sharedCount > 0) lm->sharedCount--;

    /* Determine if releasing triggers a waiting start */
    int triggered = 0;
    if (lm->sharedCount == 0 && lm->exclusiveUser == -1 && lm->qCount > 0) {
        triggered = 1;
    }

    pthread_cond_broadcast(&lm->cond);
    pthread_mutex_unlock(&lm->mutex);

    if (triggered)
        printf("User %d: end. Release lock. Next waiting may start.\n", userId);
    else
        printf("User %d: end. Release lock. Nothing else.\n", userId);
}

/* Release Exclusive Lock */
static void releaseXLock(TableId t, int userId) {
    LockManager *lm = &gLockTable[t];

    pthread_mutex_lock(&lm->mutex);
    lm->exclusiveUser = -1;

    int triggered = (lm->qCount > 0) ? 1 : 0;

    pthread_cond_broadcast(&lm->cond);
    pthread_mutex_unlock(&lm->mutex);

    if (triggered)
        printf("User %d: end. Release lock. Next waiting may start.\n", userId);
    else
        printf("User %d: end. Release lock. Nothing else.\n", userId);
}

/*
Concurrency Demo (pthread)
*/

typedef struct {
    int userId;
    TableId tableId;
    char lockType;         /* 'S' for read, 'X' for write */
    char opText[80];       /* e.g., "select visitors" */
    int durationSeconds;   /* simulate work duration */
} DemoOp;

static void* demoThreadFunc(void *arg) {
    DemoOp *op = (DemoOp*)arg;

    if (op->lockType == 'S') {
        acquireSLock(op->tableId, op->userId, op->opText);

        /* Simulated READ work */
        sleep(op->durationSeconds);

        releaseSLock(op->tableId, op->userId);
    } else {
        acquireXLock(op->tableId, op->userId, op->opText);

        sleep(op->durationSeconds);

        releaseXLock(op->tableId, op->userId);
    }

    return NULL;
}

/*
This demo matches this behavior:
- User1 READ visitors starts
- User2 UPDATE visitors waits (because S exists)
- User3 READ visitors starts (because S exists and no X active)
- User4 READ vehicles starts (different table, independent lock)
- User5 DELETE visitors waits (behind user2 in waiting queue)
*/
static void runConcurrencyDemo(void) {
    pthread_t th[5];
    DemoOp ops[5];

    /* User 1: select visitors (S lock) */
    ops[0].userId = 1;
    ops[0].tableId = T_VISITOR;
    ops[0].lockType = 'S';
    strcpy(ops[0].opText, "select visitors");
    ops[0].durationSeconds = 2;

    /* User 2: update visitors (X lock) */
    ops[1].userId = 2;
    ops[1].tableId = T_VISITOR;
    ops[1].lockType = 'X';
    strcpy(ops[1].opText, "update visitors");
    ops[1].durationSeconds = 2;

    /* User 3: select visitors (S lock) */
    ops[2].userId = 3;
    ops[2].tableId = T_VISITOR;
    ops[2].lockType = 'S';
    strcpy(ops[2].opText, "select visitors");
    ops[2].durationSeconds = 2;

    /* User 4: select vehicles (S lock) */
    ops[3].userId = 4;
    ops[3].tableId = T_VEHICLE;
    ops[3].lockType = 'S';
    strcpy(ops[3].opText, "select vehicles");
    ops[3].durationSeconds = 2;

    /* User 5: delete visitors (X lock) */
    ops[4].userId = 5;
    ops[4].tableId = T_VISITOR;
    ops[4].lockType = 'X';
    strcpy(ops[4].opText, "delete visitors");
    ops[4].durationSeconds = 2;

    printf("\n===== Concurrency Control Demo =====\n");

    /* Create threads with small delays so output order resembles the sample the Dr provided on teams */
    pthread_create(&th[0], NULL, demoThreadFunc, &ops[0]);
    usleep(150000);

    pthread_create(&th[1], NULL, demoThreadFunc, &ops[1]);
    usleep(150000);

    pthread_create(&th[2], NULL, demoThreadFunc, &ops[2]);
    usleep(150000);

    pthread_create(&th[3], NULL, demoThreadFunc, &ops[3]);
    usleep(150000);

    pthread_create(&th[4], NULL, demoThreadFunc, &ops[4]);

    /* Join all */
    for (int i = 0; i < 5; i++) {
        pthread_join(th[i], NULL);
    }

    printf("===== Demo Finished =====\n\n");
}

/* visitor */
void selectAllVisitors_locked(int userId) {
    acquireSLock(T_VISITOR, userId, "select visitors");
    selectAllVisitors();
    releaseSLock(T_VISITOR, userId);
}

int insertVisitor_locked(int userId, Visitor v) {
    acquireXLock(T_VISITOR, userId, "insert visitors");
    pthread_mutex_lock(&gLogMutex);
    setCurrentUserID(userId);
    int ok = insertVisitor(v);
    pthread_mutex_unlock(&gLogMutex);
    releaseXLock(T_VISITOR, userId);
    return ok;
}

int updateVisitor_locked(int userId, Visitor v) {
    acquireXLock(T_VISITOR, userId, "update visitors");
    pthread_mutex_lock(&gLogMutex);
    setCurrentUserID(userId);
    int ok = updateVisitor(v);
    pthread_mutex_unlock(&gLogMutex);
    releaseXLock(T_VISITOR, userId);
    return ok;
}

int deleteVisitor_locked(int userId, int visitorId) {
    acquireXLock(T_VISITOR, userId, "delete visitors");
    pthread_mutex_lock(&gLogMutex);
    setCurrentUserID(userId);
    int ok = deleteVisitor(visitorId);
    pthread_mutex_unlock(&gLogMutex);
    releaseXLock(T_VISITOR, userId);
    return ok;
}

/* vehicle */
void selectAllVehicles_locked(int userId) {
    acquireSLock(T_VEHICLE, userId, "select vehicles");
    selectAllVehicles();
    releaseSLock(T_VEHICLE, userId);
}

int insertVehicle_locked(int userId, Vehicle v) {
    acquireXLock(T_VEHICLE, userId, "insert vehicles");
    pthread_mutex_lock(&gLogMutex);
    setCurrentUserID(userId);
    int ok = insertVehicle(v);
    pthread_mutex_unlock(&gLogMutex);
    releaseXLock(T_VEHICLE, userId);
    return ok;
}

int updateVehicle_locked(int userId, Vehicle v) {
    acquireXLock(T_VEHICLE, userId, "update vehicles");
    pthread_mutex_lock(&gLogMutex);
    setCurrentUserID(userId);
    int ok = updateVehicle(v);
    pthread_mutex_unlock(&gLogMutex);
    releaseXLock(T_VEHICLE, userId);
    return ok;
}

int deleteVehicle_locked(int userId, int vehicleId) {
    acquireXLock(T_VEHICLE, userId, "delete vehicles");
    pthread_mutex_lock(&gLogMutex);
    setCurrentUserID(userId);
    int ok = deleteVehicle(vehicleId);
    pthread_mutex_unlock(&gLogMutex);
    releaseXLock(T_VEHICLE, userId);
    return ok;
}

/* host */
void selectAllHosts_locked(int userId) {
    acquireSLock(T_HOST, userId, "select hosts");
    selectAllHosts();
    releaseSLock(T_HOST, userId);
}

int insertHost_locked(int userId, Host h) {
    acquireXLock(T_HOST, userId, "insert hosts");
    pthread_mutex_lock(&gLogMutex);
    setCurrentUserID(userId);
    int ok = insertHost(h);
    pthread_mutex_unlock(&gLogMutex);
    releaseXLock(T_HOST, userId);
    return ok;
}

int updateHost_locked(int userId, Host h) {
    acquireXLock(T_HOST, userId, "update hosts");
    pthread_mutex_lock(&gLogMutex);
    setCurrentUserID(userId);
    int ok = updateHost(h);
    pthread_mutex_unlock(&gLogMutex);
    releaseXLock(T_HOST, userId);
    return ok;
}

int deleteHost_locked(int userId, int hostId) {
    acquireXLock(T_HOST, userId, "delete hosts");
    pthread_mutex_lock(&gLogMutex);
    setCurrentUserID(userId);
    int ok = deleteHost(hostId);
    pthread_mutex_unlock(&gLogMutex);
    releaseXLock(T_HOST, userId);
    return ok;
}

/* visit */
void selectAllVisits_locked(int userId) {
    acquireSLock(T_VISIT, userId, "select visits");
    selectAllVisits();
    releaseSLock(T_VISIT, userId);
}

int insertVisit_locked(int userId, Visit v) {
    acquireXLock(T_VISIT, userId, "insert visits");
    pthread_mutex_lock(&gLogMutex);
    setCurrentUserID(userId);
    int ok = insertVisit(v);
    pthread_mutex_unlock(&gLogMutex);
    releaseXLock(T_VISIT, userId);
    return ok;
}

int updateVisit_locked(int userId, Visit v) {
    acquireXLock(T_VISIT, userId, "update visits");
    pthread_mutex_lock(&gLogMutex);
    setCurrentUserID(userId);
    int ok = updateVisit(v);
    pthread_mutex_unlock(&gLogMutex);
    releaseXLock(T_VISIT, userId);
    return ok;
}

int deleteVisit_locked(int userId, int visitId) {
    acquireXLock(T_VISIT, userId, "delete visits");
    pthread_mutex_lock(&gLogMutex);
    setCurrentUserID(userId);
    int ok = deleteVisit(visitId);
    pthread_mutex_unlock(&gLogMutex);
    releaseXLock(T_VISIT, userId);
    return ok;
}

/* access code */
void selectAllAccessCodes_locked(int userId) {
    acquireSLock(T_ACCESSCODE, userId, "select accesscodes");
    selectAllAccessCodes();
    releaseSLock(T_ACCESSCODE, userId);
}

int insertAccessCode_locked(int userId, AccessCode a) {
    acquireXLock(T_ACCESSCODE, userId, "insert accesscodes");
    pthread_mutex_lock(&gLogMutex);
    setCurrentUserID(userId);
    int ok = insertAccessCode(a);
    pthread_mutex_unlock(&gLogMutex);
    releaseXLock(T_ACCESSCODE, userId);
    return ok;
}

int updateAccessCode_locked(int userId, AccessCode a) {
    acquireXLock(T_ACCESSCODE, userId, "update accesscodes");
    pthread_mutex_lock(&gLogMutex);
    setCurrentUserID(userId);
    int ok = updateAccessCode(a);
    pthread_mutex_unlock(&gLogMutex);
    releaseXLock(T_ACCESSCODE, userId);
    return ok;
}

int deleteAccessCode_locked(int userId, int accessCodeId) {
    acquireXLock(T_ACCESSCODE, userId, "delete accesscodes");
    pthread_mutex_lock(&gLogMutex);
    setCurrentUserID(userId);
    int ok = deleteAccessCode(accessCodeId);
    pthread_mutex_unlock(&gLogMutex);
    releaseXLock(T_ACCESSCODE, userId);
    return ok;
}

/* gate */
void selectAllGates_locked(int userId) {
    acquireSLock(T_GATE, userId, "select gates");
    selectAllGates();
    releaseSLock(T_GATE, userId);
}

int insertGate_locked(int userId, Gate g) {
    acquireXLock(T_GATE, userId, "insert gates");
    pthread_mutex_lock(&gLogMutex);
    setCurrentUserID(userId);
    int ok = insertGate(g);
    pthread_mutex_unlock(&gLogMutex);
    releaseXLock(T_GATE, userId);
    return ok;
}

int updateGate_locked(int userId, Gate g) {
    acquireXLock(T_GATE, userId, "update gates");
    pthread_mutex_lock(&gLogMutex);
    setCurrentUserID(userId);
    int ok = updateGate(g);
    pthread_mutex_unlock(&gLogMutex);
    releaseXLock(T_GATE, userId);
    return ok;
}

int deleteGate_locked(int userId, int gateId) {
    acquireXLock(T_GATE, userId, "delete gates");
    pthread_mutex_lock(&gLogMutex);
    setCurrentUserID(userId);
    int ok = deleteGate(gateId);
    pthread_mutex_unlock(&gLogMutex);
    releaseXLock(T_GATE, userId);
    return ok;
}

/* movement */
void selectAllMovements_locked(int userId) {
    acquireSLock(T_MOVEMENT, userId, "select movements");
    selectAllMovements();
    releaseSLock(T_MOVEMENT, userId);
}

int insertMovement_locked(int userId, Movement m) {
    acquireXLock(T_MOVEMENT, userId, "insert movements");
    pthread_mutex_lock(&gLogMutex);
    setCurrentUserID(userId);
    int ok = insertMovement(m);
    pthread_mutex_unlock(&gLogMutex);
    releaseXLock(T_MOVEMENT, userId);
    return ok;
}

int updateMovement_locked(int userId, Movement m) {
    acquireXLock(T_MOVEMENT, userId, "update movements");
    pthread_mutex_lock(&gLogMutex);
    setCurrentUserID(userId);
    int ok = updateMovement(m);
    pthread_mutex_unlock(&gLogMutex);
    releaseXLock(T_MOVEMENT, userId);
    return ok;
}

int deleteMovement_locked(int userId, int movementId) {
    acquireXLock(T_MOVEMENT, userId, "delete movements");
    pthread_mutex_lock(&gLogMutex);
    setCurrentUserID(userId);
    int ok = deleteMovement(movementId);
    pthread_mutex_unlock(&gLogMutex);
    releaseXLock(T_MOVEMENT, userId);
    return ok;
}

/* incident */
void selectAllIncidents_locked(int userId) {
    acquireSLock(T_INCIDENT, userId, "select incidents");
    selectAllIncidents();
    releaseSLock(T_INCIDENT, userId);
}

int insertIncident_locked(int userId, Incident in) {
    acquireXLock(T_INCIDENT, userId, "insert incidents");
    pthread_mutex_lock(&gLogMutex);
    setCurrentUserID(userId);
    int ok = insertIncident(in);
    pthread_mutex_unlock(&gLogMutex);
    releaseXLock(T_INCIDENT, userId);
    return ok;
}

int updateIncident_locked(int userId, Incident in) {
    acquireXLock(T_INCIDENT, userId, "update incidents");
    pthread_mutex_lock(&gLogMutex);
    setCurrentUserID(userId);
    int ok = updateIncident(in);
    pthread_mutex_unlock(&gLogMutex);
    releaseXLock(T_INCIDENT, userId);
    return ok;
}

int deleteIncident_locked(int userId, int incidentId) {
    acquireXLock(T_INCIDENT, userId, "delete incidents");
    pthread_mutex_lock(&gLogMutex);
    setCurrentUserID(userId);
    int ok = deleteIncident(incidentId);
    pthread_mutex_unlock(&gLogMutex);
    releaseXLock(T_INCIDENT, userId);
    return ok;
}


/*
INIT / CLEANUP
*/

static void initTables(void) {
    memset(&gVisitors, 0, sizeof(gVisitors));
    memset(&gVehicles, 0, sizeof(gVehicles));
    memset(&gHosts, 0, sizeof(gHosts));
    memset(&gVisits, 0, sizeof(gVisits));
    memset(&gAccessCodes, 0, sizeof(gAccessCodes));
    memset(&gGates, 0, sizeof(gGates));
    memset(&gMovements, 0, sizeof(gMovements));
    memset(&gIncidents, 0, sizeof(gIncidents));
}

static void initHashing(void) {
    dummyItem = (DataItem*)malloc(sizeof(DataItem));
    if (!dummyItem) { printf("ERROR: Out of memory.\n"); exit(1); }
    dummyItem->key = -1;
    dummyItem->data = -1;

    hashInit(visitorHash);
    hashInit(vehicleHash);
    hashInit(hostHash);
    hashInit(visitHash);
    hashInit(accessCodeHash);
    hashInit(gateHash);
    hashInit(movementHash);
    hashInit(incidentHash);
}

static void freeAllMemory(void) {
    /* Free dynamic tables */
    if (gVisitors.rows) free(gVisitors.rows);
    if (gVehicles.rows) free(gVehicles.rows);
    if (gHosts.rows) free(gHosts.rows);
    if (gVisits.rows) free(gVisits.rows);
    if (gAccessCodes.rows) free(gAccessCodes.rows);
    if (gGates.rows) free(gGates.rows);
    if (gMovements.rows) free(gMovements.rows);
    if (gIncidents.rows) free(gIncidents.rows);

    gVisitors.rows = NULL; gVisitors.count = 0; gVisitors.cap = 0;
    gVehicles.rows = NULL; gVehicles.count = 0; gVehicles.cap = 0;
    gHosts.rows = NULL;    gHosts.count = 0;    gHosts.cap = 0;
    gVisits.rows = NULL;   gVisits.count = 0;   gVisits.cap = 0;
    gAccessCodes.rows = NULL; gAccessCodes.count = 0; gAccessCodes.cap = 0;
    gGates.rows = NULL;    gGates.count = 0;    gGates.cap = 0;
    gMovements.rows = NULL; gMovements.count = 0; gMovements.cap = 0;
    gIncidents.rows = NULL; gIncidents.count = 0; gIncidents.cap = 0;

    /* Free hash items */
    hashFreeItems(visitorHash);
    hashFreeItems(vehicleHash);
    hashFreeItems(hostHash);
    hashFreeItems(visitHash);
    hashFreeItems(accessCodeHash);
    hashFreeItems(gateHash);
    hashFreeItems(movementHash);
    hashFreeItems(incidentHash);

    if (dummyItem) {
        free(dummyItem);
        dummyItem = NULL;
    }
}

static void printCounts(void) {
    printf("Loaded counts:\n");
    printf("Visitors:    %d\n", gVisitors.count);
    printf("Vehicles:    %d\n", gVehicles.count);
    printf("Hosts:       %d\n", gHosts.count);
    printf("Visits:      %d\n", gVisits.count);
    printf("AccessCodes: %d\n", gAccessCodes.count);
    printf("Gates:       %d\n", gGates.count);
    printf("Movements:   %d\n", gMovements.count);
    printf("Incidents:   %d\n", gIncidents.count);
}

/*
CLI Menus
*/

static int gSessionUserId = 1;

static void readLinePrompt(const char *prompt, char *buf, int size) {
    if (!buf || size <= 0) return;
    printf("%s", prompt);
    fflush(stdout);
    if (fgets(buf, size, stdin) == NULL) {
        buf[0] = '\0';
        return;
    }

    if (strchr(buf, '\n') == NULL) { // user wrote more than the buffer size
        int c;
        while ((c = getchar()) != '\n' && c != EOF) { }
    }

    trimNewline(buf);
}

static int readIntPrompt(const char *prompt) {
    char tmp[64];
    readLinePrompt(prompt, tmp, sizeof(tmp));
    return atoi(tmp);
}

/* Blank -> return nullValue (useful for nullable ints like GateID) */
static int readIntOptionalPrompt(const char *prompt, int nullValue) {
    char tmp[64];
    readLinePrompt(prompt, tmp, sizeof(tmp));
    if (tmp[0] == '\0') return nullValue;
    return atoi(tmp);
}

static int readMenuChoice(const char *prompt, int min, int max) {
    while (1) {
        int c = readIntPrompt(prompt);
        if (c >= min && c <= max) return c;
        printf("Invalid choice. Enter %d to %d.\n", min, max);
    }
}

static void fillNowIfEmpty(char *dtField, int size) {
    if (!dtField) return;
    if (dtField[0] == '\0') getCurrentDateTime(dtField, size);
}

static void loginMenu(void) {
    int uid = readIntPrompt("Enter UserID (integer): ");
    if (uid <= 0) uid = 1;
    gSessionUserId = uid;
    setCurrentUserID(gSessionUserId);
    printf("Logged in as User %d.\n", gSessionUserId);
}

/* -------- VISITOR MENU -------- */
static void menuVisitors(void) {
    while (1) {
        printf("\n=== Visitor Menu (User %d) ===\n", gSessionUserId);
        printf("1) List Visitors\n2) Insert Visitor\n3) Update Visitor\n4) Delete Visitor\n0) Back\n");
        int ch = readMenuChoice("Choice: ", 0, 4);

        if (ch == 0) return;

        if (ch == 1) {
            selectAllVisitors_locked(gSessionUserId);
        } else if (ch == 2) {
            Visitor v; memset(&v, 0, sizeof(v));
            v.VisitorID = readIntPrompt("VisitorID: ");
            readLinePrompt("NationalID: ", v.NationalID, sizeof(v.NationalID));
            readLinePrompt("FirstName: ", v.FirstName, sizeof(v.FirstName));
            readLinePrompt("LastName: ", v.LastName, sizeof(v.LastName));
            readLinePrompt("Phone: ", v.Phone, sizeof(v.Phone));
            readLinePrompt("Email (optional): ", v.Email, sizeof(v.Email));
            insertVisitor_locked(gSessionUserId, v);
        } else if (ch == 3) {
            Visitor v; memset(&v, 0, sizeof(v));
            v.VisitorID = readIntPrompt("VisitorID to update: ");
            readLinePrompt("NationalID: ", v.NationalID, sizeof(v.NationalID));
            readLinePrompt("FirstName: ", v.FirstName, sizeof(v.FirstName));
            readLinePrompt("LastName: ", v.LastName, sizeof(v.LastName));
            readLinePrompt("Phone: ", v.Phone, sizeof(v.Phone));
            readLinePrompt("Email (optional): ", v.Email, sizeof(v.Email));
            updateVisitor_locked(gSessionUserId, v);
        } else if (ch == 4) {
            int id = readIntPrompt("VisitorID to delete: ");
            deleteVisitor_locked(gSessionUserId, id);
        }
    }
}

/* -------- VEHICLE MENU -------- */
static void menuVehicles(void) {
    while (1) {
        printf("\n=== Vehicle Menu (User %d) ===\n", gSessionUserId);
        printf("1) List Vehicles\n2) Insert Vehicle\n3) Update Vehicle\n4) Delete Vehicle\n0) Back\n");
        int ch = readMenuChoice("Choice: ", 0, 4);

        if (ch == 0) return;

        if (ch == 1) {
            selectAllVehicles_locked(gSessionUserId);
        } else if (ch == 2) {
            Vehicle v; memset(&v, 0, sizeof(v));
            v.VehicleID = readIntPrompt("VehicleID: ");
            v.VisitorID = readIntPrompt("VisitorID (FK): ");
            readLinePrompt("PlateNumber: ", v.PlateNumber, sizeof(v.PlateNumber));
            readLinePrompt("Type: ", v.Type, sizeof(v.Type));
            readLinePrompt("Color: ", v.Color, sizeof(v.Color));
            readLinePrompt("Manufacturer: ", v.Manufacturer, sizeof(v.Manufacturer));
            readLinePrompt("Model: ", v.Model, sizeof(v.Model));
            insertVehicle_locked(gSessionUserId, v);
        } else if (ch == 3) {
            Vehicle v; memset(&v, 0, sizeof(v));
            v.VehicleID = readIntPrompt("VehicleID to update: ");
            v.VisitorID = readIntPrompt("VisitorID (FK): ");
            readLinePrompt("PlateNumber: ", v.PlateNumber, sizeof(v.PlateNumber));
            readLinePrompt("Type: ", v.Type, sizeof(v.Type));
            readLinePrompt("Color: ", v.Color, sizeof(v.Color));
            readLinePrompt("Manufacturer: ", v.Manufacturer, sizeof(v.Manufacturer));
            readLinePrompt("Model: ", v.Model, sizeof(v.Model));
            updateVehicle_locked(gSessionUserId, v);
        } else if (ch == 4) {
            int id = readIntPrompt("VehicleID to delete: ");
            deleteVehicle_locked(gSessionUserId, id);
        }
    }
}

/* -------- HOST MENU -------- */
static void menuHosts(void) {
    while (1) {
        printf("\n=== Host Menu (User %d) ===\n", gSessionUserId);
        printf("1) List Hosts\n2) Insert Host\n3) Update Host\n4) Delete Host\n0) Back\n");
        int ch = readMenuChoice("Choice: ", 0, 4);

        if (ch == 0) return;

        if (ch == 1) {
            selectAllHosts_locked(gSessionUserId);
        } else if (ch == 2) {
            Host h; memset(&h, 0, sizeof(h));
            h.HostID = readIntPrompt("HostID: ");
            readLinePrompt("FirstName: ", h.FirstName, sizeof(h.FirstName));
            readLinePrompt("LastName: ", h.LastName, sizeof(h.LastName));
            readLinePrompt("Organization: ", h.Organization, sizeof(h.Organization));
            readLinePrompt("Phone: ", h.Phone, sizeof(h.Phone));
            readLinePrompt("Email: ", h.Email, sizeof(h.Email));
            insertHost_locked(gSessionUserId, h);
        } else if (ch == 3) {
            Host h; memset(&h, 0, sizeof(h));
            h.HostID = readIntPrompt("HostID to update: ");
            readLinePrompt("FirstName: ", h.FirstName, sizeof(h.FirstName));
            readLinePrompt("LastName: ", h.LastName, sizeof(h.LastName));
            readLinePrompt("Organization: ", h.Organization, sizeof(h.Organization));
            readLinePrompt("Phone: ", h.Phone, sizeof(h.Phone));
            readLinePrompt("Email: ", h.Email, sizeof(h.Email));
            updateHost_locked(gSessionUserId, h);
        } else if (ch == 4) {
            int id = readIntPrompt("HostID to delete: ");
            deleteHost_locked(gSessionUserId, id);
        }
    }
}

/* -------- VISIT MENU -------- */
static void menuVisits(void) {
    while (1) {
        printf("\n=== Visit Menu (User %d) ===\n", gSessionUserId);
        printf("1) List Visits\n2) Insert Visit\n3) Update Visit\n4) Delete Visit\n0) Back\n");
        int ch = readMenuChoice("Choice: ", 0, 4);

        if (ch == 0) return;

        if (ch == 1) {
            selectAllVisits_locked(gSessionUserId);
        } else if (ch == 2 || ch == 3) {
            Visit v; memset(&v, 0, sizeof(v));
            if (ch == 2) v.VisitID = readIntPrompt("VisitID: ");
            else         v.VisitID = readIntPrompt("VisitID to update: ");

            v.VisitorID = readIntPrompt("VisitorID (FK): ");
            v.HostID = readIntPrompt("HostID (FK): ");
            readLinePrompt("Purpose: ", v.Purpose, sizeof(v.Purpose));
            readLinePrompt("Destination: ", v.Destination, sizeof(v.Destination));
            readLinePrompt("ScheduledDate (YYYY-MM-DD): ", v.ScheduledDate, sizeof(v.ScheduledDate));
            readLinePrompt("StartTime (HH:MM:SS): ", v.StartTime, sizeof(v.StartTime));
            readLinePrompt("EndTime (HH:MM:SS): ", v.EndTime, sizeof(v.EndTime));
            readLinePrompt("Status (Pending/Approved/...): ", v.Status, sizeof(v.Status));
            readLinePrompt("CreatedAt (blank = now): ", v.CreatedAt, sizeof(v.CreatedAt));
            readLinePrompt("ApprovedAt (blank allowed): ", v.ApprovedAt, sizeof(v.ApprovedAt));

            fillNowIfEmpty(v.CreatedAt, sizeof(v.CreatedAt));
            if (strcmp(v.Status, "Approved") == 0 && v.ApprovedAt[0] == '\0') {
                /* autofill when Approved */
                fillNowIfEmpty(v.ApprovedAt, sizeof(v.ApprovedAt));
            }

            if (ch == 2) insertVisit_locked(gSessionUserId, v);
            else         updateVisit_locked(gSessionUserId, v);
        } else if (ch == 4) {
            int id = readIntPrompt("VisitID to delete: ");
            deleteVisit_locked(gSessionUserId, id);
        }
    }
}

/* -------- ACCESSCODE MENU -------- */
static void menuAccessCodes(void) {
    while (1) {
        printf("\n=== AccessCode Menu (User %d) ===\n", gSessionUserId);
        printf("1) List AccessCodes\n2) Insert AccessCode\n3) Update AccessCode\n4) Delete AccessCode\n0) Back\n");
        int ch = readMenuChoice("Choice: ", 0, 4);

        if (ch == 0) return;

        if (ch == 1) {
            selectAllAccessCodes_locked(gSessionUserId);
        } else if (ch == 2 || ch == 3) {
            AccessCode a; memset(&a, 0, sizeof(a));
            if (ch == 2) a.AccessCodeID = readIntPrompt("AccessCodeID: ");
            else         a.AccessCodeID = readIntPrompt("AccessCodeID to update: ");

            a.VisitID = readIntPrompt("VisitID (FK/UNIQUE): ");
            readLinePrompt("CodeValue: ", a.CodeValue, sizeof(a.CodeValue));
            readLinePrompt("IssuedAt (blank = now): ", a.IssuedAt, sizeof(a.IssuedAt));
            readLinePrompt("ValidFrom (YYYY-MM-DD HH:MM:SS): ", a.ValidFrom, sizeof(a.ValidFrom));
            readLinePrompt("ValidTo (YYYY-MM-DD HH:MM:SS): ", a.ValidTo, sizeof(a.ValidTo));
            readLinePrompt("CodeStatus (Active/Revoked/Expired): ", a.CodeStatus, sizeof(a.CodeStatus));

            fillNowIfEmpty(a.IssuedAt, sizeof(a.IssuedAt));

            if (ch == 2) insertAccessCode_locked(gSessionUserId, a);
            else         updateAccessCode_locked(gSessionUserId, a);
        } else if (ch == 4) {
            int id = readIntPrompt("AccessCodeID to delete: ");
            deleteAccessCode_locked(gSessionUserId, id);
        }
    }
}

/* -------- GATE MENU -------- */
static void menuGates(void) {
    while (1) {
        printf("\n=== Gate Menu (User %d) ===\n", gSessionUserId);
        printf("1) List Gates\n2) Insert Gate\n3) Update Gate\n4) Delete Gate\n0) Back\n");
        int ch = readMenuChoice("Choice: ", 0, 4);

        if (ch == 0) return;

        if (ch == 1) {
            selectAllGates_locked(gSessionUserId);
        } else if (ch == 2 || ch == 3) {
            Gate g; memset(&g, 0, sizeof(g));
            if (ch == 2) g.GateID = readIntPrompt("GateID: ");
            else         g.GateID = readIntPrompt("GateID to update: ");
            readLinePrompt("GateName: ", g.GateName, sizeof(g.GateName));

            if (ch == 2) insertGate_locked(gSessionUserId, g);
            else         updateGate_locked(gSessionUserId, g);
        } else if (ch == 4) {
            int id = readIntPrompt("GateID to delete: ");
            deleteGate_locked(gSessionUserId, id);
        }
    }
}

/* -------- MOVEMENT MENU -------- */
static void menuMovements(void) {
    while (1) {
        printf("\n=== Movement Menu (User %d) ===\n", gSessionUserId);
        printf("1) List Movements\n2) Insert Movement\n3) Update Movement\n4) Delete Movement\n0) Back\n");
        int ch = readMenuChoice("Choice: ", 0, 4);

        if (ch == 0) return;

        if (ch == 1) {
            selectAllMovements_locked(gSessionUserId);
        } else if (ch == 2 || ch == 3) {
            Movement m; memset(&m, 0, sizeof(m));
            if (ch == 2) m.MovementID = readIntPrompt("MovementID: ");
            else         m.MovementID = readIntPrompt("MovementID to update: ");

            m.VisitID = readIntPrompt("VisitID (FK/UNIQUE): ");
            m.AccessCodeID = readIntPrompt("AccessCodeID (FK/UNIQUE): ");
            m.VehicleID = readIntPrompt("VehicleID (FK): ");
            m.EntryGateID = readIntOptionalPrompt("EntryGateID (blank = NULL): ", -1);
            m.ExitGateID  = readIntOptionalPrompt("ExitGateID (blank = NULL): ", -1);

            readLinePrompt("EntryTime (blank allowed): ", m.EntryTime, sizeof(m.EntryTime));
            readLinePrompt("ExitTime  (blank allowed): ", m.ExitTime, sizeof(m.ExitTime));

            if (ch == 2) insertMovement_locked(gSessionUserId, m);
            else         updateMovement_locked(gSessionUserId, m);
        } else if (ch == 4) {
            int id = readIntPrompt("MovementID to delete: ");
            deleteMovement_locked(gSessionUserId, id);
        }
    }
}

/* -------- INCIDENT MENU -------- */
static void menuIncidents(void) {
    while (1) {
        printf("\n=== Incident Menu (User %d) ===\n", gSessionUserId);
        printf("1) List Incidents\n2) Insert Incident\n3) Update Incident\n4) Delete Incident\n0) Back\n");
        int ch = readMenuChoice("Choice: ", 0, 4);

        if (ch == 0) return;

        if (ch == 1) {
            selectAllIncidents_locked(gSessionUserId);
        } else if (ch == 2 || ch == 3) {
            Incident in; memset(&in, 0, sizeof(in));
            if (ch == 2) in.IncidentID = readIntPrompt("IncidentID: ");
            else         in.IncidentID = readIntPrompt("IncidentID to update: ");

            in.VisitID = readIntPrompt("VisitID (FK): ");
            readLinePrompt("IncidentType: ", in.IncidentType, sizeof(in.IncidentType));
            readLinePrompt("Description: ", in.Description, sizeof(in.Description));
            readLinePrompt("ReportedAt (blank = now): ", in.ReportedAt, sizeof(in.ReportedAt));
            readLinePrompt("IncidentStatus (Open/Resolved): ", in.IncidentStatus, sizeof(in.IncidentStatus));

            fillNowIfEmpty(in.ReportedAt, sizeof(in.ReportedAt));

            if (ch == 2) insertIncident_locked(gSessionUserId, in);
            else         updateIncident_locked(gSessionUserId, in);
        } else if (ch == 4) {
            int id = readIntPrompt("IncidentID to delete: ");
            deleteIncident_locked(gSessionUserId, id);
        }
    }
}

static void runCli(void) {
    while (1) {
        printf("\n============================\n");
        printf(" VMS CLI  | Current User: %d\n", gSessionUserId);
        printf("============================\n");
        printf("1) Login / Change User\n");
        printf("2) Visitors\n");
        printf("3) Vehicles\n");
        printf("4) Hosts\n");
        printf("5) Visits\n");
        printf("6) AccessCodes\n");
        printf("7) Gates\n");
        printf("8) Movements\n");
        printf("9) Incidents\n");
        printf("10) Concurrency Demo\n");
        printf("11) Print Loaded Counts\n");
        printf("12) Reload All Tables From CSV\n");
        printf("0) Exit\n");

        int ch = readMenuChoice("Choice: ", 0, 12);

        if (ch == 0) return;
        if (ch == 1) loginMenu();
        else if (ch == 2) menuVisitors();
        else if (ch == 3) menuVehicles();
        else if (ch == 4) menuHosts();
        else if (ch == 5) menuVisits();
        else if (ch == 6) menuAccessCodes();
        else if (ch == 7) menuGates();
        else if (ch == 8) menuMovements();
        else if (ch == 9) menuIncidents();
        else if (ch == 10) runConcurrencyDemo();
        else if (ch == 11) printCounts();
        else if (ch == 12) {
            loadAllTables();
            printf("Reloaded all tables from CSV.\n");
            printCounts();
        }
    }
}


/*
MAIN
*/
int main(void) {
    initTables();
    initHashing();
    initLockTable();

    /* default login */
    gSessionUserId = 1;
    setCurrentUserID(gSessionUserId);

    loadAllTables();
    printCounts();

    runCli();

    freeAllMemory();
    return 0;
}

import {Component} from '@angular/core';
import {AngularFireAuth} from 'angularfire2/auth';
import {AngularFirestore, AngularFirestoreCollection, AngularFirestoreDocument, DocumentChangeAction, QueryDocumentSnapshot} from 'angularfire2/firestore';
import * as firebase from 'firebase/app';
import {from, Observable, Observer} from 'rxjs';
import {bufferCount, concat, concatMap, map, mergeMap, take, tap, toArray} from 'rxjs/operators';

@Component({
  selector: 'app-root',
  templateUrl: './app.component.html',
  styleUrls: ['./app.component.css']
})
export class AppComponent {
  readonly title = 'Ledger Cloud Dashboard';
  readonly batchSize = 25;

  authenticated = false;
  uid = null;
  version = '0';
  loading = true;
  deleteInProgress = false;

  user: Observable<firebase.User>;
  usersCollection: AngularFirestoreCollection;
  userDocument: AngularFirestoreDocument<any>;
  versionsCollection: AngularFirestoreCollection;
  versionDocument: AngularFirestoreDocument<any>;
  devicesCollection: AngularFirestoreCollection;
  devices: Observable<any>;
  allCollections: Array<AngularFirestoreCollection> = [];
  deletedDocumentCount = 0;

  constructor(public afAuth: AngularFireAuth, private db: AngularFirestore) {
    this.user = afAuth.authState;

    this.afAuth.authState.subscribe((state) => {
      this.authenticated = state != null;
      if (this.authenticated) {
        this.uid = state.uid;
        this.loadVersions();
      }
    });
  }

  login() {
    let provider = new firebase.auth.GoogleAuthProvider();
    provider.setCustomParameters({prompt: 'select_account'});
    this.afAuth.auth.signInWithPopup(provider);
  }

  logout() {
    this.afAuth.auth.signOut();
  }

  rootPath() {
    return 'users/' + this.uid;
  }

  versionDocumentPath(version: number) {
    let path = `${this.rootPath()}/${version}/default_document`;
    console.log(path);
    return path;
  }

  versionPath() {
    return this.rootPath() + '/' + this.version;
  }

  userDevicesPath() {
    return this.versionPath() + '/default_document/devices';
  }

  loadVersions() {
    this.usersCollection = this.db.collection('users');
    this.userDocument = this.usersCollection.doc(this.uid);
    this.versionsCollection = this.userDocument.collection('versions');
    this.versionsCollection.snapshotChanges()
        .pipe(this.unwrap())
        .subscribe(versionObjects => {
          let nextVersion = '0';
          for (const versionObject of versionObjects) {
            if (Number(versionObject.id) > Number(nextVersion)) {
              nextVersion = versionObject.id;
            }
          }
          this.version = nextVersion;
          this.versionDocument = this.versionsCollection.doc(this.version);
          this.loading = false;
          this.loadDevices();
          this.loadAllCollections();
        });
  }

  loadDevices() {
    this.devicesCollection = this.versionDocument.collection('devices');
    this.devices = this.devicesCollection.snapshotChanges().pipe(
        map(actions => actions.map(a => {
          const data = a.payload.doc.data();
          const id = a.payload.doc.id;
          return {id, ...data};
        })));
  }

  // Assembles the list of all collections of the user.
  loadAllCollections() {
    const schema = {
      'devices': {},
      'namespaces': {'pages': {'commit-log': {}, 'objects': {}}}
    };
    this.loadNested(this.versionsCollection, schema)
        .pipe(toArray())
        .subscribe((result) => this.allCollections = result);
  }

  // Returns an observable which recursively streams nested collections under
  // |collection| according to the given schema.
  loadNested(collection: AngularFirestoreCollection, schema):
      Observable<AngularFirestoreCollection> {
    return collection.snapshotChanges().pipe(
        this.unwrap(), take(1),

        mergeMap(documents => {
          let result = [from([collection])];
          for (const document of documents) {
            for (const subcollectionName in schema) {
              const subcollection =
                  collection.doc(document.id).collection(subcollectionName);
              const subcollectionObservable =
                  this.loadNested(subcollection, schema[subcollectionName]);
              result.push(subcollectionObservable);
            }
          }
          return result;
        }),
        mergeMap((a) => a));
  }

  // Pipeable operator which unwraps document snapshots so that they contain the
  // key as |id| along with data fields.
  unwrap() {
    return map((actions: DocumentChangeAction<any>[]) => actions.map(a => {
      const data = a.payload.doc.data();
      const id = a.payload.doc.id;
      return {id, ...data};
    }))
  }

  erase() {
    this.deleteInProgress = true;
    this.deletedDocumentCount = 0;
    this.eraseCollections(this.allCollections);
  }

  // Erases all documents in the given collection in batches.
  eraseCollections(collections: Array<AngularFirestoreCollection>) {
    from(collections)
        .pipe(
            concatMap(
                collection => collection.snapshotChanges().pipe(
                    take(1), concatMap((v) => v))),
            bufferCount(this.batchSize),
            concatMap((documents) => this.deleteDocs(documents)))
        .subscribe(
            (count: number) => {
              this.deletedDocumentCount += count;
            },
            (err) => {
              console.log(err);
            },
            () => {
              this.deleteInProgress = false;
              this.loadVersions();
            });
  }

  // Returns an Observable which deletes the given documents and streams the
  // number of deleted documents.
  deleteDocs(documents: DocumentChangeAction<any>[]): Observable<number> {
    let result = Observable.create((o: Observer<number>) => {
      let batch = this.db.firestore.batch();
      for (const document in documents) {
        batch.delete(documents[document].payload.doc.ref);
      }
      batch.commit().then(() => {
        o.next(documents.length);
        o.complete();
      });
    });
    return result;
  }
}

# Cloud dashboard

Web dashboard allowing a signed-in user to inspect their Ledger state and
trigger cloud erase if needed.

This project is built using Angular was originally scaffolded using  version 1.2.1.

## Development workflow

Make sure that you have [nodejs](https://nodejs.org) and [Angular CLI](https://github.com/angular/angular-cli) installed, then run `npm install` in the `cloud_dashboard` directory.

Then, you can use Angular CLI (`ng`):

 - run `ng serve` for a dev server. Navigate to `http://localhost:4200/`. The app will automatically reload if you change any of the source files
 - run `ng generate component component-name` to generate a new component. You can also use `ng generate directive|pipe|service|class|module`
 - run `ng test` to execute the unit tests via [Karma](https://karma-runner.github.io)

To get more help on the Angular CLI use `ng help` or go check out the [Angular CLI README](https://github.com/angular/angular-cli/blob/master/README.md).

## Deployment

Make sure that you have [Firebase CLI](https://firebase.google.com/docs/cli/)
installed, and run `firebase login` at least once. Then:

 - run `ng build -prod` to build the project. The build artifacts will be stored
   in the `dist/` directory
 - run `firebase deploy`

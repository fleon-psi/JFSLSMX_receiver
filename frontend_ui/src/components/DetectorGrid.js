
import React, {Component} from 'react';

import Grid from '@material-ui/core/Grid';
import Paper from '@material-ui/core/Paper';

function formatPedestal(val) {
    var x = Number(val);
    if (x === 0) return "0";
    return x.toPrecision(5);
}

class DetectorGrid extends Component {
  render() {
     return <Grid container spacing={3}>
        {this.props.modules.map((module) => (
         <Grid item xs={6}>
          <Paper style={{textAlign: 'center'}}>
              {module.name}<br/>
              Bad pixels: {module.bad_pixels}<br/>
              Mean pedestal G0: {formatPedestal(module.meanG0)}<br/>
              Mean pedestal G1: {formatPedestal(module.meanG1)}<br/>
              Mean pedestal G2: {formatPedestal(module.meanG2)}<br/>
          </Paper>
         </Grid>))}
     </Grid>
  }
}

export default DetectorGrid;

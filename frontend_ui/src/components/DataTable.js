
import React, {Component} from 'react';

import TableContainer from '@material-ui/core/TableContainer';
import Table from '@material-ui/core/Table';
import Paper from '@material-ui/core/Paper';
import TableBody from '@material-ui/core/TableBody';
import TableRow from '@material-ui/core/TableRow';
import TableCell from '@material-ui/core/TableCell';

class DataTable extends Component {
  render() {
     return <TableContainer component={Paper} style={{width: 650, marginLeft: "auto", marginRight: "auto"}}>
      <Table size="small" aria-label="simple table">
          <TableBody>
            {this.props.params.map((param) => (
            <TableRow>
              <TableCell component="th" scope="row"> {param.desc} </TableCell>
              <TableCell align="right">{param.value}</TableCell>
            </TableRow>
            ))}
          </TableBody>
      </Table>
    </TableContainer>
  }
}

export default DataTable;
